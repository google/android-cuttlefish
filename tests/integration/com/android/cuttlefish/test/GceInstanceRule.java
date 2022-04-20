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

import static com.google.common.collect.ImmutableList.toImmutableList;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.junit.Assume.assumeNoException;
import static org.junit.Assume.assumeNotNull;
import static org.junit.Assume.assumeTrue;

import com.android.cuttlefish.test.DataType;
import com.android.cuttlefish.test.Exit;
import com.android.cuttlefish.test.TestMessage;
import com.android.ddmlib.Log.LogLevel;
import com.android.tradefed.invoker.TestInformation;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.util.FileUtil;
import com.google.auto.value.AutoValue;
import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.google.protobuf.ByteString;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.util.UUID;
import javax.annotation.Nullable;
import org.junit.rules.TestRule;
import org.junit.runner.Description;
import org.junit.runners.model.Statement;

/**
 * Manager for a dedicated GCE instance for every @Test function.
 *
 * Must be constructed through Guice injection. Calls out to the cvd_test_gce_driver binary to
 * create the GCE instances.
 */
public final class GceInstanceRule implements TestRule {
  @Inject(optional = true)
  @SetOption("gce-driver-service-account-json-key-path")
  @Nullable
  private String gceJsonKeyPath = null;

  @Inject(optional = true) @SetOption("cloud-project") private String cloudProject;

  @Inject(optional = true) @SetOption("zone") private String zone = "us-west1-a";

  @Inject(optional = true)
  @SetOption("internal-addresses")
  private boolean internal_addresses = false;

  @Inject private TestInformation testInfo;
  @Inject private BuildChooser buildChooser;

  private Process driverProcess;
  private String managedInstance;

  private Process launchDriver(File gceDriver) throws IOException {
    ImmutableList.Builder<String> cmdline = new ImmutableList.Builder();
    cmdline.add(gceDriver.toString());
    assumeNotNull(gceJsonKeyPath);
    cmdline.add("--internal-addresses=" + internal_addresses);
    cmdline.add("--cloud-project=" + cloudProject);
    cmdline.add("--service-account-json-private-key-path=" + gceJsonKeyPath);
    ProcessBuilder processBuilder = new ProcessBuilder(cmdline.build());
    processBuilder.redirectInput(ProcessBuilder.Redirect.PIPE);
    processBuilder.redirectOutput(ProcessBuilder.Redirect.PIPE);
    processBuilder.redirectError(ProcessBuilder.Redirect.PIPE);
    return processBuilder.start();
  }

  private static Thread launchLogger(InputStream input) {
    BufferedReader reader = new BufferedReader(new InputStreamReader(input));
    Thread logThread = new Thread(() -> {
      try {
        String line;
        while ((line = reader.readLine()) != null) {
          CLog.logAndDisplay(LogLevel.DEBUG, "cvd_test_gce_driver output: %s", line);
        }
      } catch (Exception e) {
        CLog.logAndDisplay(LogLevel.DEBUG, "cvd_test_gce_driver exception: %s", e);
      }
    });
    logThread.start();
    return logThread;
  }

  private String createInstance() throws IOException {
    String desiredName = "cuttlefish-integration-" + UUID.randomUUID();
    TestMessage.Builder request = TestMessage.newBuilder();
    request.getCreateInstanceBuilder().getIdBuilder().setName(desiredName);
    request.getCreateInstanceBuilder().getIdBuilder().setZone(zone);
    sendMessage(request.build());
    ImmutableList<TestMessage> errors =
        collectResponses().stream().filter(TestMessage::hasError).collect(toImmutableList());
    if (errors.size() > 0) {
      throw new IOException("Failed to create instance: " + errors);
    }
    return desiredName;
  }

  @AutoValue
  public static abstract class SshResult {
    public static SshResult create(File stdout, File stderr, int ret) {
      return new AutoValue_GceInstanceRule_SshResult(stdout, stderr, ret);
    }

    public abstract File stdout();
    public abstract File stderr();
    public abstract int returnCode();
  }

  public SshResult ssh(String... command) throws IOException {
    return ssh(ImmutableList.copyOf(command));
  }

  public SshResult ssh(ImmutableList<String> command) throws IOException {
    TestMessage.Builder request = TestMessage.newBuilder();
    request.getSshCommandBuilder().getInstanceBuilder().setName(managedInstance);
    request.getSshCommandBuilder().addAllArguments(command);
    sendMessage(request.build());
    File stdout = FileUtil.createTempFile("ssh_", "_stdout.txt");
    OutputStream stdoutStream = new FileOutputStream(stdout);
    File stderr = FileUtil.createTempFile("ssh_", "_stderr.txt");
    OutputStream stderrStream = new FileOutputStream(stderr);
    int returnCode = -1;
    IOException storedException = null;
    while (true) {
      TestMessage response = receiveMessage();
      switch (response.getContentsCase()) {
        case DATA:
          if (response.getData().getType().equals(DataType.DATA_TYPE_STDOUT)) {
            stdoutStream.write(response.getData().getContents().toByteArray());
          } else if (response.getData().getType().equals(DataType.DATA_TYPE_STDERR)) {
            stderrStream.write(response.getData().getContents().toByteArray());
          } else if (response.getData().getType().equals(DataType.DATA_TYPE_RETURN_CODE)) {
            returnCode = Integer.valueOf(response.getData().getContents().toStringUtf8());
          } else {
            throw new RuntimeException("Unexpected type: " + response.getData().getType());
          }
          break;
        case ERROR:
          if (storedException == null) {
            storedException = new IOException(response.getError().getText());
          } else {
            storedException.addSuppressed(new IOException(response.getError().getText()));
          }
        case STREAM_END:
          if (storedException == null) {
            return SshResult.create(stdout, stderr, returnCode);
          } else {
            throw storedException;
          }
        default: {
          IOException exception = new IOException("Unexpected message: " + response);
          if (storedException == null) {
            exception.addSuppressed(storedException);
          }
          throw exception;
        }
      }
    }
  }

  public void uploadFile(File sourceFile, String destFile) throws IOException {
    TestMessage.Builder request = TestMessage.newBuilder();
    request.getUploadFileBuilder().getInstanceBuilder().setName(managedInstance);
    request.getUploadFileBuilder().setRemotePath(destFile);

    // Allow this to error out before initiating the transfer
    FileInputStream stream = new FileInputStream(sourceFile);
    sendMessage(request.build());
    byte[] buffer = new byte[1 << 14 /* 16 KiB */];
    int read = 0;
    while ((read = stream.read(buffer)) != -1) {
      TestMessage.Builder dataMessage = TestMessage.newBuilder();
      dataMessage.getDataBuilder().setType(DataType.DATA_TYPE_FILE_CONTENTS);
      dataMessage.getDataBuilder().setContents(ByteString.copyFrom(buffer, 0, read));
      sendMessage(dataMessage.build());
    }
    TestMessage.Builder endRequest = TestMessage.newBuilder();
    endRequest.setStreamEnd(StreamEnd.getDefaultInstance());
    sendMessage(endRequest.build());
    ImmutableList<TestMessage> errors =
        collectResponses().stream().filter(TestMessage::hasError).collect(toImmutableList());
    if (errors.size() > 0) {
      throw new IOException("Failed to upload file: " + errors);
    }
  }

  public void uploadBuildArtifact(String artifact, String destFile) throws IOException {
    TestMessage.Builder request = TestMessage.newBuilder();
    request.getUploadBuildArtifactBuilder().getInstanceBuilder().setName(managedInstance);
    request.getUploadBuildArtifactBuilder().setBuild(buildChooser.buildProto());
    request.getUploadBuildArtifactBuilder().setArtifactName(artifact);
    request.getUploadBuildArtifactBuilder().setRemotePath(destFile);
    sendMessage(request.build());
    ImmutableList<TestMessage> errors =
        collectResponses().stream().filter(TestMessage::hasError).collect(toImmutableList());
    if (errors.size() > 0) {
      throw new IOException("Failed to upload build artifact: " + errors);
    }
  }

  private TestMessage receiveMessage() throws IOException {
    TestMessage message = TestMessage.parser().parseDelimitedFrom(driverProcess.getInputStream());
    CLog.logAndDisplay(LogLevel.DEBUG, "Received message \"" + message + "\"");
    return message;
  }

  private ImmutableList<TestMessage> collectResponses() throws IOException {
    ImmutableList.Builder<TestMessage> messages = ImmutableList.builder();
    while (true) {
      TestMessage received = receiveMessage();
      messages.add(received);
      if (received.hasStreamEnd()) {
        return messages.build();
      }
    }
  }

  private void sendMessage(TestMessage message) throws IOException {
    CLog.logAndDisplay(LogLevel.DEBUG, "Sending message \"" + message + "\"");
    message.writeDelimitedTo(driverProcess.getOutputStream());
    driverProcess.getOutputStream().flush();
  }

  @Override
  public Statement apply(Statement base, Description description) {
    final File gceDriver;
    try {
      gceDriver = testInfo.getDependencyFile("cvd_test_gce_driver", false);
    } catch (FileNotFoundException e) {
      assumeNoException("Could not find cvd_test_gce_driver", e);
      return null;
    }
    assumeTrue("cvd_test_gce_driver file did not exist", gceDriver.exists());
    return new Statement() {
      @Override
      public void evaluate() throws Throwable {
        // TODO(schuffelen): Reuse instances with GCE resets.
        // The trick will be figuring out when the instances can actually be destroyed.
        driverProcess = launchDriver(gceDriver);
        Thread logStderr = launchLogger(driverProcess.getErrorStream());
        managedInstance = createInstance();
        try {
          base.evaluate();
        } finally {
          boolean cleanExit = false;
          for (int i = 0; i < 10; i++) {
            sendMessage(TestMessage.newBuilder().setExit(Exit.getDefaultInstance()).build());
            TestMessage response = receiveMessage();
            if (response.hasExit()) {
              cleanExit = true;
              break;
            } else if (!response.hasError()
                && !response.hasStreamEnd()) { // Swallow some errors to to get out if necessary
              throw new AssertionError("Unexpected message " + response);
            }
          }
          assertTrue("Failed to get an exit response", cleanExit);
          assertEquals(0, driverProcess.waitFor());
          logStderr.join();
          driverProcess = null;
        }
      }
    };
  }
}
