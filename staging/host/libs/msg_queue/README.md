# msg_queue

This is the Cuttlefish message queue wrapper library, as one of the IPC options available.  Below are the example usages running in two separate processes.

```
#define MAX_MSG_SIZE 200
#define NUM_MESSAGES 100

typedef struct msg_buffer {
	long mesg_type;
	char mesg_text[MAX_MSG_SIZE];
} msg_buffer;

int example_send()
{
  const std::string message ="Test message"
  auto msg_queue = SysVMessageQueue::Create("unique_queue_name", false);
  if (msg_queue == NULL) {
    LOG(FATAL) << "Create: failed to create" << "unique_queue_name";
  }

  struct msg_buffer msg;
  msg.mesg_type = 1;
  strcpy(msg.mesg_text, message.c_str());
  int rc = msg_queue->Send(&msg, message.length() + 1, true);
  if (rc == -1) {
    LOG(FATAL) << "Send: failed to send message to msg_queue";
  }
}
```

```
#define MAX_MSG_SIZE 200

typedef struct msg_buffer {
	long mesg_type;
	char mesg_text[MAX_MSG_SIZE];
} msg_buffer;

int example_receive()
{
  auto msg_queue = SysVMessageQueue::Create("unique_queue_name");
  if (msg_queue == NULL) {
    LOG(FATAL) << "create: failed to create" << "unique_queue_name";
  }

  struct msg_buffer msg = {0, {0}};
  int rc = msg_queue->Receive(&msg, MAX_MSG_SIZE, 1, true);
  if (rc == -1) {
	LOG(FATAL) << "receive: failed to receive any messages";
  }

  std::string text(msg.mesg_text);
  LOG(INFO) << "Metrics host received: " << text;
	return 0;
}
```
