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
    // create message queue with the key 'a'
	int queue_id = msg_queue_create('a');
	struct msg_buffer msg;
	for (int i=1; i <= NUM_MESSAGES; i++) {
        // create message types 1-3
		msg.mesg_type = (i % 3) + 1;
		sprintf(msg.mesg_text, "test %d", i);
		int rc = msg_queue_send(queue_id, &msg, strlen(msg.mesg_text)+1, false);
		if (rc == -1) {
			perror("main: msgsnd");
			exit(1);
		}
	}
	printf("generated %d messages, exiting.\n", NUM_MESSAGES);
	return 0;
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
    // create message queue with the key 'a'
	int queue_id = msg_queue_create('a');
	struct msg_buffer msg;
	while (1) {
		int rc = msg_queue_receive(queue_id, &msg, MAX_MSG_SIZE, 1, false);
		if (rc == -1) {
			perror("main: msgrcv");
			exit(1);
		}
		printf("Reader '%d' read message: '%s'\n", 1, msg.mesg_text);
	}
	return 0;
}
```
