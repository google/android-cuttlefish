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

type cvdHostPackage struct {
	android.ModuleBase
	android.PackagingBase
}

func cvdHostPackageFactory() android.Module {
	module := &cvdHostPackage{}
	android.InitPackageModule(module)
	android.InitAndroidArchModule(module, android.HostSupported, android.MultilibFirst)
	module.IgnoreMissingDependencies = true
	return module
}

type dependencyTag struct {
	blueprint.BaseDependencyTag
	android.InstallAlwaysNeededDependencyTag // to force installation of both "deps" and manually added dependencies
	android.PackagingItemAlwaysDepTag  // to force packaging of both "deps" and manually added dependencies
}

var cvdHostPackageDependencyTag = dependencyTag{}

func (c *cvdHostPackage) DepsMutator(ctx android.BottomUpMutatorContext) {
	c.AddDeps(ctx, cvdHostPackageDependencyTag)

	variations := []blueprint.Variation{
		{Mutator: "os", Variation: ctx.Target().Os.String()},
		{Mutator: "arch", Variation: android.Common.String()},
	}
	for _, dep := range strings.Split(
		ctx.Config().VendorConfig("cvd").String("launch_configs"), " ") {
		if ctx.OtherModuleExists(dep) {
			ctx.AddVariationDependencies(variations, cvdHostPackageDependencyTag, dep)
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
	zipFile := android.PathForModuleOut(ctx, "package.zip")
	c.CopyDepsToZip(ctx, zipFile)

	// Dir where to extract the zip file and construct the final tar.gz from
	packageDir := android.PathForModuleOut(ctx, ".temp")
	builder := android.NewRuleBuilder(pctx, ctx)
	builder.Command().
		BuiltTool("zipsync").
		FlagWithArg("-d ", packageDir.String()).
		Input(zipFile)

	output := android.PathForModuleOut(ctx, "package.tar.gz")
	builder.Command().Text("tar Scfz").
		Output(output).
		FlagWithArg("-C ", packageDir.String()).
		Flag("--mtime='2020-01-01'"). // to have reproducible builds
		Text(".")

	builder.Command().Text("rm").Flag("-rf").Text(packageDir.String())

	builder.Build("cvd_host_package", fmt.Sprintf("Packaging %s", c.BaseModuleName()))

	ctx.InstallFile(android.PathForModuleInstall(ctx), c.BaseModuleName()+".tar.gz", output)
}
