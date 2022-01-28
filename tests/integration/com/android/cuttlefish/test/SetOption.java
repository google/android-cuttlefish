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

import static java.lang.annotation.ElementType.FIELD;
import static java.lang.annotation.ElementType.METHOD;
import static java.lang.annotation.ElementType.PARAMETER;
import static java.lang.annotation.RetentionPolicy.RUNTIME;

import java.lang.annotation.Retention;
import java.lang.annotation.Target;
import javax.inject.Qualifier;

/**
 * Binding annotation for Tradefed options.
 *
 * If an option is given to tradefed in the form of
 *
 * <pre>
 *   --test-arg com.android.tradefed.testtype.HostTest:set-option:OPTION:VALUE
 * </pre>
 *
 * {@link CuttlefishIntegrationTestRunner} will create a binding of {@code SetOption("OPTION")}
 * and to a string with contents {@code VALUE}.
 */
@Qualifier
@Target({FIELD, PARAMETER, METHOD})
@Retention(RUNTIME)
public @interface SetOption {
  String value();
}
