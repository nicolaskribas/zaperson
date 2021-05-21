#include "settings.h"
#include <MQTTAsync.h>
#include <ctype.h>
#include <ncurses.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef struct {
	char from[MAX_ID_LEN];
	char message[MAX_MESSAGE_LEN];
} Message;

typedef struct {
	char topic[MAX_ID_LEN];
	char with[MAX_ID_LEN];
} ConversationHeader;

typedef struct {
	char topic[MAX_ID_LEN];
	char with[MAX_ID_LEN];
	int begin;
	int messagesCount;
	Message messages[MAX_MESSAGES_HISTORY];
} Conversation;

struct ZapersonClient_s {
	MQTTAsync MQTTClient;
	const char *clientID;
	const char *ctrlTopic;
	const char *clientTopic;
} my;

typedef struct ZapersonClient_s ZapersonClient;

bool setupFinished = false;

int selector = 0;
bool home = true;

int groupsCount;
const char **groups;

pthread_mutex_t conversationsMutex = PTHREAD_MUTEX_INITIALIZER;
int conversationsCount;
Conversation conversations[MAX_CONCURRENT_CONVERSATIONS];
void redraw();

char *combineStr(const char first[], const char second[]) {
	size_t resultSize = strlen(first) + strlen(second) + 1;
	char *result = malloc(resultSize * sizeof(char));
	strcpy(result, first);
	strcat(result, second);
	return result;
}

bool canAddConversation() {
	bool result;

	pthread_mutex_lock(&conversationsMutex);
	if (conversationsCount < MAX_CONCURRENT_CONVERSATIONS) {
		result = true;
	} else {
		result = false;
	}
	pthread_mutex_unlock(&conversationsMutex);

	return result;
}

void addConversation(ConversationHeader *ch) {
	pthread_mutex_lock(&conversationsMutex);
	strcpy(conversations[conversationsCount].topic, ch->topic);
	strcpy(conversations[conversationsCount].with, ch->with);
	conversations[conversationsCount].begin = 0;
	conversations[conversationsCount].messagesCount = 0;
	conversationsCount++;
	pthread_mutex_unlock(&conversationsMutex);
	redraw();
}

void stopUI();
void onConnLost(void *context, char *cause) {

	MQTTAsync_connectOptions connOpts = MQTTAsync_connectOptions_initializer;
	connOpts.cleansession = 0;

	int rc;
	if ((rc = MQTTAsync_connect(my.MQTTClient, &connOpts)) != MQTTASYNC_SUCCESS) {
		stopUI();
		printf("Failed to start sendMessage, return code %d\n", rc);
		exit(EXIT_FAILURE);
	}
}

void addConversationToList(void *context, MQTTAsync_successData *response) {
	ConversationHeader *ch = context;
	addConversation(ch);
	free(ch);
}

void freeConversationHeader(void *context, MQTTAsync_failureData *response) {
	ConversationHeader *ch = context;
	free(ch);
}

void sendConversationTopicToOther(void *context,
		MQTTAsync_successData *response) {

	ConversationHeader *ch = context;

	char payload[300];
	sprintf(payload, "%s\31%s", my.clientID, ch->topic);

	MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
	opts.context = ch;
	opts.onSuccess = addConversationToList;
	opts.onFailure = freeConversationHeader;

	MQTTAsync_message msg = MQTTAsync_message_initializer;
	msg.payload = payload;
	msg.payloadlen = strlen(payload);
	msg.qos = QOS;

	char otherClientTopic[300];
	sprintf(otherClientTopic, "%s%s", ch->with, CLIENT_TOPIC_SUFFIX);

	int rc;
	if ((rc = MQTTAsync_sendMessage(my.MQTTClient, otherClientTopic, &msg,
					&opts)) != MQTTASYNC_SUCCESS) {
		stopUI();
		printf("Failed to start sendMessage, return code %d\n", rc);
		exit(EXIT_FAILURE);
	}
}

void handleCtrlMsg(MQTTAsync_message *ctrlMsg) {
	if (!canAddConversation())
		return;

	ConversationHeader *ch = malloc(sizeof(ConversationHeader));

	strcpy(ch->with, ctrlMsg->payload);
	sprintf(ch->topic, "%s_%s_%d", ch->with, my.clientID, rand() % 10000);

	char otherClientTopic[300];
	sprintf(otherClientTopic, "%s%s", ch->with, CLIENT_TOPIC_SUFFIX);

	MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
	opts.context = ch;
	opts.onSuccess = sendConversationTopicToOther;
	opts.onFailure = freeConversationHeader;

	int rc;
	if ((rc = MQTTAsync_subscribe(my.MQTTClient, ch->topic, QOS, &opts)) !=
			MQTTASYNC_SUCCESS) {
		stopUI();
		printf("Failed to start subscribe, return code %d\n", rc);
		exit(EXIT_FAILURE);
	}
}

void handleClientMsg(MQTTAsync_message *clientMsg) {
	ConversationHeader *ch = malloc(sizeof(ConversationHeader));
	sscanf(clientMsg->payload, "%[^\31]\31%[^\n]", ch->with, ch->topic);

	MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
	opts.context = ch;
	opts.onSuccess = addConversationToList;
	opts.onFailure = freeConversationHeader;
	int rc;
	if ((rc = MQTTAsync_subscribe(my.MQTTClient, ch->topic, QOS, &opts)) !=
			MQTTASYNC_SUCCESS) {
		stopUI();
		printf("Failed to start subscribe, return code %d\n", rc);
		exit(EXIT_FAILURE);
	}
}

void addMessageToConversation(char *from, char *msg, Conversation *cnvrstn) {
	if (cnvrstn->messagesCount == MAX_MESSAGES_HISTORY) {
		strcpy(cnvrstn->messages[cnvrstn->begin].from, from);
		strcpy(cnvrstn->messages[cnvrstn->begin].message, msg);
		cnvrstn->begin = (cnvrstn->begin + 1) % MAX_MESSAGES_HISTORY;
	} else {
		strcpy(cnvrstn->messages[cnvrstn->messagesCount].from, from);
		strcpy(cnvrstn->messages[cnvrstn->messagesCount].message, msg);
		cnvrstn->messagesCount++;
	}
	redraw();
}

void handleUserMsg(char topicName[], MQTTAsync_message *msg) {
	char from[MAX_ID_LEN];
	char content[MAX_MESSAGE_LEN];

	sscanf(msg->payload, "%[^\31]\31%[^\n]", from, content);

	pthread_mutex_lock(&conversationsMutex);
	for (int i = 0; i < conversationsCount; i++) {
		if (strcmp(topicName, conversations[i].topic) == 0) {
			addMessageToConversation(from, content, &conversations[i]);
			break;
		}
	}
	pthread_mutex_unlock(&conversationsMutex);
}

int onMsgArrvd(void *context, char *topicName, int topicLen,
		MQTTAsync_message *message) {

	if (strcmp(topicName, my.ctrlTopic) == 0) {
		handleCtrlMsg(message);
	} else if (strcmp(topicName, my.clientTopic) == 0) {
		handleClientMsg(message);
	} else {
		handleUserMsg(topicName, message);
	}

	MQTTAsync_free(topicName);
	MQTTAsync_freeMessage(&message);
	return 1;
}

void onStartConnFail(void *context, MQTTAsync_failureData *response) {
	printf("Failed to connect to server %s, rc: %d, message: %s\n", SERVER_URI,
			response->code,
			response->message != NULL ? response->message : "No message");
	exit(EXIT_FAILURE);
}

void onClientSubFail(void *context, MQTTAsync_failureData *response) {
	printf("Failed to subscribe client topic, rc: %d, message: %s\n",
			response->code,
			response->message != NULL ? response->message : "No message");
	exit(EXIT_FAILURE);
}

void onGroupSubSucc(void *context, MQTTAsync_successData *response) {
	ConversationHeader *ch = context;
	addConversation(ch);
	free(ch);
}

void onGroupSubFail(void *context, MQTTAsync_failureData *response) {
	ConversationHeader *ch = context;
	free(ch);
}

// subscrite to groups topics
void onClientSubSucc(void *context, MQTTAsync_successData *response) {
	setupFinished = true;

	MQTTAsync_responseOptions respOpts = MQTTAsync_responseOptions_initializer;
	respOpts.onSuccess = onGroupSubSucc;
	respOpts.onFailure = onGroupSubFail;

	groupsCount = MIN(groupsCount, MAX_CONCURRENT_CONVERSATIONS);
	for (int i = 0; i < groupsCount; i++) {
		ConversationHeader *ch = malloc(sizeof(ConversationHeader));
		sprintf(ch->topic, "%s%s", groups[i], GROUP_TOPIC_SUFFIX);
		strcpy(ch->with, groups[i]);
		respOpts.context = ch;

		int rc;
		if ((rc = MQTTAsync_subscribe(my.MQTTClient, ch->topic, QOS, &respOpts)) !=
				MQTTASYNC_SUCCESS) {
			printf("Failed to start subscribe to group topic, return code %d\n", rc);
			exit(EXIT_FAILURE);
		}
	}
}

// subscribe to client topic
void onCtrlSubSucc(void *context, MQTTAsync_successData *response) {
	MQTTAsync_responseOptions respOpts = MQTTAsync_responseOptions_initializer;
	respOpts.onSuccess = onClientSubSucc;
	respOpts.onFailure = onClientSubFail;

	int rc;
	if ((rc = MQTTAsync_subscribe(my.MQTTClient, my.clientTopic, QOS,
					&respOpts)) != MQTTASYNC_SUCCESS) {
		printf("Failed to start subscribe to client topic, return code %d\n", rc);
		exit(EXIT_FAILURE);
	}
}

void onCtrlSubFail(void *context, MQTTAsync_failureData *response) {
	printf("Failed to subscribe control topic, rc: %d, message: %s\n",
			response->code,
			response->message != NULL ? response->message : "No message");
	exit(EXIT_FAILURE);
}

// subscribe to ctrl topic
void onStartConnSucc(void *context, MQTTAsync_successData *response) {
	MQTTAsync_responseOptions respOpts = MQTTAsync_responseOptions_initializer;
	respOpts.onSuccess = onCtrlSubSucc;
	respOpts.onFailure = onCtrlSubFail;

	int rc;
	if ((rc = MQTTAsync_subscribe(my.MQTTClient, my.ctrlTopic, QOS, &respOpts)) !=
			MQTTASYNC_SUCCESS) {
		printf("Failed to start subscribe to ctrl topic, return code %d\n", rc);
		exit(EXIT_FAILURE);
	}
}

void stopUI() { endwin(); }

void requestNewConversationWith(char otherId[]) {
	if (!canAddConversation())
		return;

	char *otherCtrlTopic = combineStr(otherId, CTRL_TOPIC_SUFFIX);

	MQTTAsync_message msg = MQTTAsync_message_initializer;
	msg.payload = (void *)my.clientID;
	msg.payloadlen = strlen(my.clientID);
	msg.qos = QOS;

	int rc;
	if ((rc = MQTTAsync_sendMessage(my.MQTTClient, otherCtrlTopic, &msg, NULL)) !=
			MQTTASYNC_SUCCESS) {
		stopUI();
		printf("Failed to start sendMessage, return code %d\n", rc);
		exit(EXIT_FAILURE);
	}

	free(otherCtrlTopic);
}

char buf[MAX_MESSAGE_LEN] = "";

void cleanBuf() { buf[0] = '\0'; }

void goToChat() { home = false; }

void handleEnterHome() {
	if (strlen(buf) > 0) {
		requestNewConversationWith(buf);
		cleanBuf();
	} else {
		if (conversationsCount > 0)
			goToChat();
	}
}

void sendMessage() {
	char *chatTopic = conversations[selector].topic;

	char payload[300];

	sprintf(payload, "%s\31%s", my.clientID, buf);

	MQTTAsync_message msg = MQTTAsync_message_initializer;
	msg.payload = payload;
	msg.payloadlen = strlen(payload);
	msg.qos = QOS;

	int rc;
	if ((rc = MQTTAsync_sendMessage(my.MQTTClient, chatTopic, &msg, NULL)) !=
			MQTTASYNC_SUCCESS) {
		stopUI();
		printf("Failed to start sendMessage, return code %d\n", rc);
		exit(EXIT_FAILURE);
	}

	cleanBuf();
}

void handleEnterInChat() {
	if (strlen(buf) > 0) {
		sendMessage();
	}
}
void handleEnter() {
	if (home) {
		handleEnterHome();
	} else {
		handleEnterInChat();
	}
}

void draw();

void waitInputAndRedraw() {
	int ch;
	move(LINES - 3, 3 + strlen(buf));
	ch = getch();
	if (ch == ERR)
		return;

	if (ch == KEY_UP || ch == 259) {
		selector = MAX(selector - 1, 0);
	} else if (ch == KEY_DOWN || ch == 258) {
		selector = MIN(selector + 1, conversationsCount - 1);
	} else if (ch == KEY_EXIT || ch == 27) {
		if (!home) {
			home = true;
		}
	} else if (ch == KEY_ENTER || ch == 10) {
		handleEnter();
	} else if (ch == KEY_BACKSPACE || ch == 127) {
		if (strlen(buf) > 0)
			buf[strlen(buf) - 1] = '\0';
	} else if (isprint(ch)) {
		char c = ch;
		strncat(buf, &c, 1);
	} else {
		return;
	}
	redraw();
}

void drawBox(int y, int x, int length, int height) {
	// Corners
	mvaddch(y, x, ACS_ULCORNER);
	mvaddch(y, x + length - 1, ACS_URCORNER);
	mvaddch(y + height - 1, x, ACS_LLCORNER);
	mvaddch(y + height - 1, x + length - 1, ACS_LRCORNER);

	// Upper and bottom borders
	for (int i = x + 1; i < x + length - 1; i++) {
		mvaddch(y, i, ACS_HLINE);
		mvaddch(y + height - 1, i, ACS_HLINE);
	}
	// Left and right borders
	for (int j = y + 1; j < y + height - 1; j++) {
		mvaddch(j, x, ACS_VLINE);
		mvaddch(j, x + length - 1, ACS_VLINE);
	}
}

void drawInputEdges() {
	drawBox(LINES - 4, 1, COLS - 2, 3);
	mvprintw(LINES - 4, 3, "Input");
	mvprintw(LINES - 3, 3, buf);
}

void drawGlobalEdges() {
	box(stdscr, ACS_VLINE, ACS_HLINE);
	mvprintw(0, 2, "Zaperson");
}

void drawConversation(int i) {
	drawBox(2 + 3 * i, 1, COLS - 2, 3);
	mvprintw(2 + 3 * i, 3, conversations[i].with);
}

void drawMessage(int c, int i) {
	drawBox(2 + 3 * i, 1, COLS - 2, 3);
	mvprintw(2 + 3 * i, 3, conversations[c].messages[i].from);
	mvprintw(3 + 3 * i, 3, conversations[c].messages[i].message);
}

void draw() {
	drawGlobalEdges();
	drawInputEdges();
	if (home) {
		for (int i = 0; i < conversationsCount; i++) {
			drawConversation(i);
		}
		if (conversationsCount > 0)
			mvaddch(2 + 3 * selector, 2, ACS_RARROW);
	} else {
		for (int i = 0; i < conversations[selector].messagesCount; i++) {
			drawMessage(selector,
					(conversations[selector].begin + i) % MAX_MESSAGES_HISTORY);
		}
	}
}
void redraw() {
	clear();
	draw();
}

void initUI() {
	initscr();
	cbreak();
	timeout(33);
	noecho();
	keypad(stdscr, true);
	draw();
	while (true) {
		waitInputAndRedraw();
	}
}

int main(int argc, const char *argv[]) {
	if (argc < 2) {
		printf("No client id provided\n");
		exit(EXIT_FAILURE);
	}

	groupsCount = argc - 2;
	groups = argv + 2;

	my.clientID = argv[1];
	my.ctrlTopic = combineStr(my.clientID, CTRL_TOPIC_SUFFIX);
	my.clientTopic = combineStr(my.clientID, CLIENT_TOPIC_SUFFIX);

	MQTTAsync_create(&my.MQTTClient, SERVER_URI, my.clientID,
			MQTTCLIENT_PERSISTENCE_NONE, NULL);
	MQTTAsync_setCallbacks(my.MQTTClient, NULL, onConnLost, onMsgArrvd, NULL);
	MQTTAsync_connectOptions connOpts = MQTTAsync_connectOptions_initializer;
	connOpts.cleansession = 1;
	connOpts.onSuccess = onStartConnSucc;
	connOpts.onFailure = onStartConnFail;

	int rc;
	if ((rc = MQTTAsync_connect(my.MQTTClient, &connOpts)) != MQTTASYNC_SUCCESS) {
		printf("Failed to start connection, return code %d\n", rc);
		exit(EXIT_FAILURE);
	}

	while (!setupFinished) {
		sleep(1);
	}

	initUI();
}
