/*
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.android.cuttlefish.test;

import static com.google.common.base.Preconditions.checkNotNull;
import static com.google.common.base.Preconditions.checkState;

import com.android.tradefed.build.IBuildInfo;
import com.android.tradefed.config.Option;
import com.android.tradefed.invoker.TestInformation;
import com.android.tradefed.testtype.HostTest;
import com.android.tradefed.testtype.IBuildReceiver;
import com.android.tradefed.testtype.ISetOptionReceiver;
import com.android.tradefed.testtype.ITestInformationReceiver;
import com.google.auto.value.AutoAnnotation;
import com.google.common.collect.ImmutableMap;
import com.google.inject.AbstractModule;
import com.google.inject.Binder;
import com.google.inject.Guice;
import com.google.inject.Injector;
import java.io.File;
import java.io.FileNotFoundException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.regex.Pattern;
import org.junit.runners.BlockJUnit4ClassRunner;
import org.junit.runners.model.InitializationError;
import org.junit.runners.model.TestClass;

public final class CuttlefishIntegrationTestRunner extends BlockJUnit4ClassRunner
    implements ITestInformationReceiver, ISetOptionReceiver, IBuildReceiver {
  @Option(name = HostTest.SET_OPTION_NAME, description = HostTest.SET_OPTION_DESC)
  private HashSet<String> keyValueOptions = new HashSet<>();

  private IBuildInfo buildInfo;
  private TestInformation testInfo;
  private final TestClass testClass;

  // Required by JUnit
  public CuttlefishIntegrationTestRunner(Class<?> testClass) throws InitializationError {
    this(new TestClass(testClass));
  }

  private CuttlefishIntegrationTestRunner(TestClass testClass) throws InitializationError {
    super(testClass);
    this.testClass = testClass;
  }

  @Override
  public void setBuild(IBuildInfo buildInfo) {
    this.buildInfo = checkNotNull(buildInfo);
  }

  @Override
  public void setTestInformation(TestInformation testInfo) {
    this.testInfo = checkNotNull(testInfo);
  }

  @Override
  public TestInformation getTestInformation() {
    return checkNotNull(testInfo);
  }

  private ImmutableMap<String, String> processOptions() {
    // Regex from HostTest.setOptionToLoadedObject
    String delim = ":";
    String esc = "\\";
    String regex = "(?<!" + Pattern.quote(esc) + ")" + Pattern.quote(delim);
    ImmutableMap.Builder<String, String> optMap = ImmutableMap.builder();
    for (String item : keyValueOptions) {
      String[] fields = item.split(regex);
      checkState(fields.length == 2, "Could not parse \"%s\"", item);
      String value = fields[1].replaceAll(Pattern.quote(esc) + Pattern.quote(delim), delim);
      optMap.put(fields[0], value);
    }
    return optMap.build();
  }

  @AutoAnnotation
  private static SetOption setOptionAnnotation(String value) {
    return new AutoAnnotation_CuttlefishIntegrationTestRunner_setOptionAnnotation(value);
  }

  private void bindOptions(Binder binder) {
    // TODO(schuffelen): Handle collections and maps
    for (Map.Entry<String, String> option : processOptions().entrySet()) {
      SetOption annotation = setOptionAnnotation(option.getKey());
      binder.bind(String.class).annotatedWith(annotation).toInstance(option.getValue());
    }
  }

  private final class TradefedClassesModule extends AbstractModule {
    @Override
    protected void configure() {
      bind(TestInformation.class).toInstance(testInfo);
      bind(IBuildInfo.class).toInstance(buildInfo);
      bindOptions(binder());
    }
  }

  @Override
  protected Object createTest() {
    Injector injector = Guice.createInjector(new TradefedClassesModule());
    return injector.getInstance(testClass.getJavaClass());
  }
}
