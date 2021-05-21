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

bool tryAddConversation(const char with[], const char topic[]) {
	bool result;

	pthread_mutex_lock(&conversationsMutex);
	if (conversationsCount < MAX_CONCURRENT_CONVERSATIONS) {
		strcpy(conversations[conversationsCount].topic, topic);
		strcpy(conversations[conversationsCount].with, with);
		conversations[conversationsCount].begin = 0;
		conversations[conversationsCount].messagesCount = 0;
		conversationsCount++;
		result = true;
	} else {
		result = false;
	}
	pthread_mutex_unlock(&conversationsMutex);

	redraw();

	return result;
}

void onConnLost(void *context, char *cause) {
	fprintf(stderr, "onConnLost cause: %s\n", cause);

	MQTTAsync_connectOptions connOpts = MQTTAsync_connectOptions_initializer;
	connOpts.cleansession = 0;

	int rc;
	if ((rc = MQTTAsync_connect(my.MQTTClient, &connOpts)) != MQTTASYNC_SUCCESS) {
		fprintf(stderr, "Reconnection fail, return code %d\n", rc);
	}
}

void handleCtrlMsg(MQTTAsync_message *ctrlMsg) {

	char *otherId = ctrlMsg->payload;

	char cnvrstnTopic[MAX_CONVERSETION_TOPIC_LEN];
	sprintf(cnvrstnTopic, "%s_%s_%d", otherId, my.clientID, rand() % 10000);

	char otherClientTopic[40];
	sprintf(otherClientTopic, "%s%s", otherId, CLIENT_TOPIC_SUFFIX);

	if (tryAddConversation(otherId, cnvrstnTopic)) {
		int rc;
		if ((rc = MQTTAsync_subscribe(my.MQTTClient, cnvrstnTopic, QOS, NULL)) !=
				MQTTASYNC_SUCCESS) {
		}

		MQTTAsync_message msg = MQTTAsync_message_initializer;
		msg.payload = cnvrstnTopic;
		msg.payloadlen = strlen(cnvrstnTopic);
		msg.qos = QOS;

		if ((rc = MQTTAsync_sendMessage(my.MQTTClient, otherClientTopic, &msg,
						NULL)) != MQTTASYNC_SUCCESS) {
		}
	}
}

void handleClientMsg(MQTTAsync_message *clientMsg) {
	char *cnvrstnTopic = clientMsg->payload;
	int rc;
	if ((rc = MQTTAsync_subscribe(my.MQTTClient, cnvrstnTopic, QOS, NULL)) !=
			MQTTASYNC_SUCCESS) {
	}
	tryAddConversation("", cnvrstnTopic);
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
	pthread_mutex_lock(&conversationsMutex);
	for (int i = 0; i < conversationsCount; i++) {
		if (strcmp(topicName, conversations[i].topic) == 0) {
			addMessageToConversation("nsei ainda", msg->payload, &conversations[i]);
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

void onDiscSucc(void *context, MQTTAsync_successData *response) {
	fprintf(stderr, "onDiscSucc\n");
}

void onStartConnFail(void *context, MQTTAsync_failureData *response) {
	printf("onConnFail rc %d\n", response ? response->code : 0);
	exit(EXIT_FAILURE);
}

void onSubSucc(void *context, MQTTAsync_successData *response) {
	printf("onSubSucc\n");
}

void onSubFail(void *context, MQTTAsync_failureData *response) {
	printf("onSubFail rc %d\n", response ? response->code : 0);
}

void onStartConnSucc(void *context, MQTTAsync_successData *response) {
	MQTTAsync_responseOptions respOpts = MQTTAsync_responseOptions_initializer;
	respOpts.onSuccess = onSubSucc;
	respOpts.onFailure = onSubFail;

	int rc;
	if ((rc = MQTTAsync_subscribe(my.MQTTClient, my.ctrlTopic, QOS, &respOpts)) !=
			MQTTASYNC_SUCCESS) {
		printf("Failed to start subscribe to ctrl topic, return code %d\n", rc);
		exit(EXIT_FAILURE);
	}
	if ((rc = MQTTAsync_subscribe(my.MQTTClient, my.clientTopic, QOS,
					&respOpts)) != MQTTASYNC_SUCCESS) {
		printf("Failed to start subscribe to client topic, return code %d\n", rc);
		exit(EXIT_FAILURE);
	}

	char groupTopic[MAX_GROUP_TOPIC_LEN];
	for (int i = 0; i < MIN(groupsCount, MAX_CONCURRENT_CONVERSATIONS); i++) {
		sprintf(groupTopic, "%s_%s", groups[i], GROUP_TOPIC_SUFFIX);
		if ((rc = MQTTAsync_subscribe(my.MQTTClient, groupTopic, QOS, &respOpts)) !=
				MQTTASYNC_SUCCESS) {
			printf("Failed to start subscribe to group topic, return code %d\n", rc);
			exit(EXIT_FAILURE);
		}
		tryAddConversation(groups[i], groupTopic);
	}
}

void onDelivCmplt(void *context, MQTTAsync_token token) {
	fprintf(stderr, "onDelivCmplt\n");
}

void disconect(ZapersonClient *my) {
	MQTTAsync_disconnectOptions discOpts =
		MQTTAsync_disconnectOptions_initializer;
	discOpts.onSuccess = onDiscSucc;
	int rc;
	if ((rc = MQTTAsync_disconnect(my->MQTTClient, &discOpts)) !=
			MQTTASYNC_SUCCESS) {
		fprintf(stderr, "Failed to start disconnect, return code %d\n", rc);
		exit(EXIT_FAILURE);
	}
	MQTTAsync_destroy(&(my->MQTTClient));
}

void onSendFailure(void *context, MQTTAsync_failureData *response) {
	fprintf(stderr, "onSendFailure\n");
}

void createNewConversation(char otherId[]) {
	if (canAddConversation()) {
		char *otherCtrlTopic = combineStr(otherId, CTRL_TOPIC_SUFFIX);

		MQTTAsync_message msg = MQTTAsync_message_initializer;
		msg.payload = (void *)my.clientID;
		msg.payloadlen = strlen(my.clientID) + 1;
		msg.qos = QOS;

		MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
		opts.onFailure = onSendFailure;

		int rc;
		if ((rc = MQTTAsync_sendMessage(my.MQTTClient, otherCtrlTopic, &msg,
						&opts)) != MQTTASYNC_SUCCESS) {
			fprintf(stderr, "Failed to start sendMessage, return code %d\n", rc);
			exit(EXIT_FAILURE);
		}
	}
}

char buf[100] = "";

void cleanBuf() { buf[0] = '\0'; }

void goToChat() { home = false; }

void handleEnterHome() {
	if (strlen(buf) > 0) {
		createNewConversation(buf);
		cleanBuf();
	} else {
		if (conversationsCount > 0)
			goToChat();
	}
}

void sendMessage() {
	char *chatTopic = conversations[selector].topic;

	MQTTAsync_message msg = MQTTAsync_message_initializer;
	msg.payload = buf;
	msg.payloadlen = strlen(buf);
	msg.qos = QOS;

	int rc;
	if ((rc = MQTTAsync_sendMessage(my.MQTTClient, chatTopic, &msg, NULL)) !=
			MQTTASYNC_SUCCESS) {
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

void waitInput() {
	int ch;
	while (true) {
		move(LINES - 3, 3 + strlen(buf));
		ch = getch();
		if (ch == ERR)
			continue;

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
			continue;
		}
		redraw();
	}
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

void setupZapersonClient() {
	MQTTAsync_create(&my.MQTTClient, SERVER_URI, my.clientID,
			MQTTCLIENT_PERSISTENCE_NONE, NULL);
	MQTTAsync_setCallbacks(my.MQTTClient, NULL, onConnLost, onMsgArrvd, NULL);

	MQTTAsync_connectOptions connOpts = MQTTAsync_connectOptions_initializer;
	connOpts.onSuccess = onStartConnSucc;
	connOpts.onFailure = onStartConnFail;
	int rc;
	if ((rc = MQTTAsync_connect(my.MQTTClient, &connOpts)) != MQTTASYNC_SUCCESS) {
		printf("Failed to start connect, return code %d\n", rc);
		exit(EXIT_FAILURE);
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

	printf("Starting zaperson for %s\n", my.clientID);
	if (groupsCount == 0) {
		printf("Joining no groups\n");
	} else {
		printf("Joining %d group(s)\n", groupsCount);
	}

	setupZapersonClient();

	sleep(3);

	initscr();
	cbreak();
	timeout(10);
	noecho();
	keypad(stdscr, true);
	while (true) {
		redraw();
		waitInput();
	}
	endwin();
}
