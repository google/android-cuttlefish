// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package cuttlefish

import (
	"fmt"
	"strings"

	"github.com/google/blueprint"

	"android/soong/android"
)

func init() {
	android.RegisterModuleType("cvd_host_package", cvdHostPackageFactory)
}

type cvdHostPackageProperties struct {
	// list of modules to include in this package. The transitive dependencies of the deps
	// are also included.
	Deps       []string `android:"arch_variant"`
	CommonDeps []string `android:"arch_variant"`
}

type cvdHostPackage struct {
	android.ModuleBase
	properties cvdHostPackageProperties
	output     android.OutputPath
}

func cvdHostPackageFactory() android.Module {
	module := &cvdHostPackage{}
	module.AddProperties(&module.properties)
	android.InitAndroidArchModule(module, android.HostSupported, android.MultilibFirst)
	return module
}

type dependencyTag struct{ blueprint.BaseDependencyTag }

var cvdHostPackageDependencyTag = dependencyTag{}

func (c *cvdHostPackage) DepsMutator(ctx android.BottomUpMutatorContext) {
	for _, dep := range c.properties.Deps {
		if ctx.OtherModuleExists(dep) {
			ctx.AddVariationDependencies(nil, cvdHostPackageDependencyTag, dep)
		}
	}

	variations := []blueprint.Variation{
		{Mutator: "os", Variation: ctx.Target().Os.String()},
		{Mutator: "arch", Variation: android.Common.String()},
	}
	for _, dep := range c.properties.CommonDeps {
		if ctx.OtherModuleExists(dep) {
			ctx.AddFarVariationDependencies(variations, cvdHostPackageDependencyTag, dep)
		}
	}

	// If cvd_custom_action_config is set, include custom action servers in the
	// host package as specified by cvd_custom_action_servers.
	customActionConfig := ctx.Config().VendorConfig("cvd").String("custom_action_config")
	if customActionConfig != "" && ctx.OtherModuleExists(customActionConfig) {
		ctx.AddVariationDependencies(variations, cvdHostPackageDependencyTag,
			customActionConfig)
		for _, dep := range strings.Split(
			ctx.Config().VendorConfig("cvd").String("custom_action_servers"), " ") {
			if ctx.OtherModuleExists(dep) {
				ctx.AddVariationDependencies(nil, cvdHostPackageDependencyTag, dep)
			}
		}
	}
}

var pctx = android.NewPackageContext("android/soong/cuttlefish")

func (c *cvdHostPackage) GenerateAndroidBuildActions(ctx android.ModuleContext) {
	// We need paths relative to the base directory because the current directory is changed via
	// the -C option and tar needs input file paths relative to the current directory
	baseDir := android.PathForModuleInstall(ctx)

	// Host common arch are installed to out/soong/host/linux-x86 (see pathsForInstall in android/paths.go)
	osName := ctx.Os().String()
	if ctx.Os() == android.Linux {
		osName = "linux"
	}
	commonBaseDir := android.PathForOutput(ctx, "host", osName+"-x86")

	// install paths for arch-specific and common-arch files
	var inputs []android.Path
	var commonInputs []android.Path

	ctx.WalkDeps(func(child android.Module, parent android.Module) bool {
		files := child.FilesToInstall()
		// the different arch is not included. This can happen via rust.proc_macros
		myArch := ctx.Arch().ArchType
		childArch := child.Target().Arch.ArchType
		if childArch != android.Common && childArch != myArch {
			return false
		}

		// Depending on where the file is under, put it to either inputs or commonInputs
		for _, file := range files.Paths() {
			if _, rel := android.MaybeRel(ctx, baseDir.String(), file.String()); rel {
				inputs = append(inputs, file)
			} else if _, rel := android.MaybeRel(ctx, commonBaseDir.String(), file.String()); rel {
				commonInputs = append(commonInputs, file)
			}
		}
		return true
	})
	inputs = android.SortedUniquePaths(inputs)
	commonInputs = android.SortedUniquePaths(commonInputs)

	builder := android.NewRuleBuilder()

	tempZip := android.PathForModuleOut(ctx, "package.zip")
	builder.Command().BuiltTool(ctx, "soong_zip").
		FlagWithArg("-symlinks", "=false"). // do follow symlinks because cc_prebuilt_* have symlinks pointing the source path
		FlagWithOutput("-o ", tempZip).
		FlagWithArg("-C ", baseDir.String()).
		FlagForEachInput("-f ", inputs).
		FlagWithArg("-C ", commonBaseDir.String()).
		FlagForEachInput("-f ", commonInputs)

	// convert zip to tar.gz by unzipping it to a temp dir and gzipping the directory
	c.output = android.PathForModuleOut(ctx, "package.tar.gz").OutputPath
	tempDir := android.PathForModuleOut(ctx, ".temp").OutputPath
	builder.Command().BuiltTool(ctx, "zipsync").
		FlagWithArg("-d ", tempDir.String()).
		Input(tempZip)
	builder.Command().Text("tar Scfzh").
		Output(c.output).
		FlagWithArg("-C ", tempDir.String()).
		Text(".")
	builder.Command().Text("rm -rf").Text(tempDir.String())

	builder.Temporary(tempZip)
	builder.DeleteTemporaryFiles()

	builder.Build(pctx, ctx, "cvd_host_package", fmt.Sprintf("Packaging %s", c.BaseModuleName()))

	ctx.InstallFile(baseDir, c.BaseModuleName()+".tar.gz", c.output)
}
