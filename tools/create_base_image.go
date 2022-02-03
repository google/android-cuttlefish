package main

import (
  "os"
  "os/exec"
  "os/user"
  "flag"
  "fmt"
  "strings"
  "io/ioutil"
  "log"
  "time"
)

type OnFail int

const (
    IgnoreOnFail OnFail = iota
    WarnOnFail
    ExitOnFail
)

var build_instance string
var build_project string
var build_zone string
var dest_image string
var dest_family string
var dest_project string
var launch_instance string
var source_image_family string
var source_image_project string
var repository_url string
var repository_branch string
var version string
var SSH_FLAGS string
var INTERNAL_extra_source string
var verbose bool
var username string

func init() {
  user, err := user.Current()
  if err != nil {
    panic(err)
  }
  username = user.Username

  flag.StringVar(&build_instance, "build_instance",
    username+"-build", "Instance name to create for the build")
  flag.StringVar(&build_project, "build_project",
    mustShell("gcloud config get-value project"), "Project to use for scratch")
  flag.StringVar(&build_zone, "build_zone",
    mustShell("gcloud config get-value compute/zone"),
    "Zone to use for scratch resources")
  flag.StringVar(&dest_image, "dest_image",
    "vsoc-host-scratch-"+username, "Image to create")
  flag.StringVar(&dest_family, "dest_family", "",
    "Image family to add the image to")
  flag.StringVar(&dest_project, "dest_project",
    mustShell("gcloud config get-value project"), "Project to use for the new image")
  flag.StringVar(&launch_instance, "launch_instance", "",
    "Name of the instance to launch with the new image")
  flag.StringVar(&source_image_family, "source_image_family", "debian-11",
    "Image familty to use as the base")
  flag.StringVar(&source_image_project, "source_image_project", "debian-cloud",
    "Project holding the base image")
  flag.StringVar(&repository_url, "repository_url",
    "https://github.com/google/android-cuttlefish.git",
    "URL to the repository with host changes")
  flag.StringVar(&repository_branch, "repository_branch",
    "main", "Branch to check out")
  flag.StringVar(&version, "version", "", "cuttlefish-common version")
  flag.StringVar(&SSH_FLAGS, "INTERNAL_IP", "",
    "INTERNAL_IP can be set to --internal-ip run on a GCE instance."+
    "The instance will need --scope compute-rw.")
  flag.StringVar(&INTERNAL_extra_source, "INTERNAL_extra_source", "",
    "INTERNAL_extra_source may be set to a directory containing the source for extra packages to build.")
  flag.BoolVar(&verbose, "verbose", true, "print commands and output (default: true)")
  flag.Parse()
}

func shell(cmd string) (string, error) {
  if verbose {
    fmt.Println(cmd)
  }
  b, err := exec.Command("/bin/sh", "-c", cmd).CombinedOutput()
  if verbose {
    fmt.Println(string(b))
  }
  if err != nil {
    return "", err
  }
  return strings.TrimSpace(string(b)), nil
}

func mustShell(cmd string) string {
  if verbose {
    fmt.Println(cmd)
  }
  out, err := shell(cmd)
  if err != nil {
    panic(err)
  }
  if verbose {
    fmt.Println(out)
  }
  return strings.TrimSpace(out)
}

func gce(action OnFail, gceArg string, errorStr ...string) (string, error) {
  cmd := "gcloud " + gceArg
  out, err := shell(cmd)
  if out != "" {
    fmt.Println(out)
  }
  if err != nil && action != IgnoreOnFail {
    var buf string
    fmt.Sprintf(buf, "gcloud error occurred: %s", err)
    if (len(errorStr) > 0) {
      buf += " [" + errorStr[0] + "]"
    }
    if action == ExitOnFail {
      panic(buf)
    }
    if action == WarnOnFail {
      fmt.Println(buf)
    }
  }
  return out, err
}

func waitForInstance(PZ string) {
  for {
    time.Sleep(5 * time.Second)
    _, err := gce(WarnOnFail, `compute ssh `+SSH_FLAGS+` `+PZ+` `+
                  build_instance+` -- uptime`)
    if err == nil {
      break
    }
  }
}

func packageSource(url string, branch string, version string, subdir string) {
  repository_dir := url[strings.LastIndex(url, "/")+1:]
  debian_dir := mustShell(`basename "`+repository_dir+`" .git`)
  if subdir != "" {
    debian_dir = repository_dir + "/" + subdir
  }
  mustShell("git clone " + url + " -b "+branch)
  mustShell("dpkg-source -b " + debian_dir)
  mustShell("rm -rf " + debian_dir)
  mustShell("ls -l")
  mustShell("pwd")
}

func createInstance(instance string, arg string) {
  _, err := gce(WarnOnFail, `compute instances describe "`+instance+`"`)
  if err != nil {
    gce(ExitOnFail, `compute instances create `+arg+` "`+instance+`"`)
  }
}

func main() {
  gpu_type := "nvidia-tesla-p100-vws"
  PZ := "--project=" + build_project + " --zone=" + build_zone

  dest_family_flag := ""
  if dest_family != "" {
    dest_family_flag = "--family=" + dest_family
  }

  scratch_dir, err := ioutil.TempDir("", "")
  if err != nil {
    log.Fatal(err)
  }

  oldDir, err := os.Getwd()
  if err != nil {
    log.Fatal(err)
  }
  os.Chdir(scratch_dir)
  packageSource(repository_url, repository_branch, "cuttlefish-common_" + version, "")
  os.Chdir(oldDir)

  abt := os.Getenv("ANDROID_BUILD_TOP")
  source_files := `"` + abt + `/device/google/cuttlefish/tools/create_base_image_gce.sh"`
  source_files += " " + `"` + abt + `/device/google/cuttlefish/tools/update_gce_kernel.sh"`
  source_files += " " + `"` + abt + `/device/google/cuttlefish/tools/remove_old_gce_kernel.sh"`
  source_files += " " + scratch_dir + "/*"
  if INTERNAL_extra_source != "" {
    source_files += " " + INTERNAL_extra_source + "/*"
  }

  delete_instances := build_instance + " " + dest_image
  if launch_instance != "" {
    delete_instances += " " + launch_instance
  }

  gce(WarnOnFail, `compute instances delete -q `+PZ+` `+delete_instances,
    `Not running`)
  gce(WarnOnFail, `compute disks delete -q `+PZ+` "`+dest_image+
    `"`, `No scratch disk`)
  gce(WarnOnFail, `compute images delete -q --project="`+build_project+
    `" "`+dest_image+`"`, `Not respinning`)
  gce(WarnOnFail, `compute disks create `+PZ+` --image-family="`+source_image_family+
    `" --image-project="`+source_image_project+`" "`+dest_image+`"`)
  gce(ExitOnFail, `compute accelerator-types describe "`+gpu_type+`" `+PZ,
    `Please use a zone with `+gpu_type+` GPUs available.`)
  createInstance(build_instance, PZ+
    ` --machine-type=n1-standard-16 --image-family="`+source_image_family+
    `" --image-project="`+source_image_project+
    `" --boot-disk-size=200GiB --accelerator="type=`+gpu_type+
    `,count=1" --maintenance-policy=TERMINATE --boot-disk-size=200GiB`)

  waitForInstance(PZ)

  // Ubuntu tends to mount the wrong disk as root, so help it by waiting until
  // it has booted before giving it access to the clean image disk
  gce(WarnOnFail, `compute instances attach-disk `+PZ+` "`+build_instance+
    `" --disk="`+dest_image+`"`)

  // beta for the --internal-ip flag that may be passed via SSH_FLAGS
  gce(ExitOnFail, `beta compute scp `+SSH_FLAGS+` `+PZ+` `+source_files+
    ` "`+build_instance+`:"`)

  // Update the host kernel before installing any kernel modules
  // Needed to guarantee that the modules in the chroot aren't built for the
  // wrong kernel
  gce(WarnOnFail, `compute ssh `+SSH_FLAGS+` `+PZ+` "`+build_instance+
    `" -- ./update_gce_kernel.sh`)
  // TODO rammuthiah if the instance is clobbered with ssh commands within
  // 5 seconds of reboot, it becomes inaccessible. Workaround that by sleeping
  // 50 seconds.
  time.Sleep(50 * time.Second)
  gce(ExitOnFail, `compute ssh `+SSH_FLAGS+` `+PZ+` "`+build_instance+
    `" -- ./remove_old_gce_kernel.sh`)

  gce(ExitOnFail, `compute ssh `+SSH_FLAGS+` `+PZ+` "`+build_instance+
    `" -- ./create_base_image_gce.sh`)
  gce(ExitOnFail, `compute instances delete -q `+PZ+` "`+build_instance+`"`)
  gce(ExitOnFail, `compute images create --project="`+build_project+
    `" --source-disk="`+dest_image+`" --source-disk-zone="`+build_zone+
    `" --licenses=https://www.googleapis.com/compute/v1/projects/vm-options/global/licenses/enable-vmx `+
    dest_family_flag+` "`+dest_image+`"`)
  gce(ExitOnFail, `compute disks delete -q `+PZ+` "`+dest_image+`"`)

  if launch_instance != "" {
    createInstance(launch_instance, PZ+
      ` --image-project="`+build_project+`" --image="`+dest_image+
      `" --machine-type=n1-standard-4 --scopes storage-ro --accelerator="type=`+
      gpu_type+`,count=1" --maintenance-policy=TERMINATE`)
  }

  fmt.Printf("Test and if this looks good, consider releasing it via:\n"+
            "\n"+
            "gcloud compute images create \\\n"+
            "  --project=\"%s\" \\\n"+
            "  --source-image=\"%s\" \\\n"+
            "  --source-image-project=\"%s\" \\\n"+
            "  \"%s\" \\\n"+
            "  \"%s\"\n",
            dest_project, dest_image, build_project, dest_family_flag, dest_image)
}
