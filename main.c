#include <OpenDoor.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sqlite3.h>
#include <ctype.h>
#include <limits.h>

#include <stdint.h>
#include <sys/stat.h>
#include "interbbs2.h"
#include "inih/ini.h"

#define TURNS_PER_DAY 5
#define TURNS_IN_PROTECTION 0
#if _MSC_VER
#include <windows.h>
#define snprintf _snprintf
#define strcasecmp _stricmp
#endif

int turns_per_day;
int turns_in_protection;

tIBInfo InterBBSInfo;
int interBBSMode;

typedef struct player {
	int id;
	char bbsname[256];
	char gamename[17];

	int troops;
	int generals;
	int fighters;
	int defence_stations;
	int spies;

	int population;
	int food;
	int credits;

	int planets_food;
	int planets_ore;
	int planets_industrial;
	int planets_military;

	int command_ship;

	int turns_left;
	time_t last_played;
	int last_score;
	int total_turns;
} player_t;

typedef struct message {
	int id;
	char to[17];
	char from[17];
	int addr;
	int system;
	time_t date;
	int seen;
	char body[256];
} message_t;

typedef struct ibbsmsg {
	int32_t type;
	uint32_t from;
	char player_name[17];
	char victim_name[17];
	int32_t score;
	int32_t troops;
	int32_t generals;
	int32_t fighters;
	int32_t plunder_credits;
	int32_t plunder_food;
	int32_t plunder_people;
	char message[256];
	uint32_t created;
} __attribute__((packed)) ibbsmsg_t;

typedef struct ibbsscore {
	char player_name[17];
	char bbs_name[40];
	int score;
} ibbsscores_t;


static int handler(void* user, const char* section, const char* name,
                   const char* value)
{
	if (strcasecmp(section, "main") == 0) {
		if (strcasecmp(name, "turns per day") == 0) {
			turns_per_day = atoi(value);
		} else if (strcasecmp(name, "turns in protection") == 0) {
			turns_in_protection = atoi(value);
		}
	} else if (strcasecmp(section, "interbbs") == 0) {
		if (strcasecmp(name, "enabled") == 0) {
			if (strcasecmp(value, "true") == 0) {
				interBBSMode = 1;
			} else {
				interBBSMode = 0;
			}
		} else if (strcasecmp(name, "system name") == 0) {
			strncpy(InterBBSInfo.myNode->name, value, SYSTEM_NAME_CHARS);
			InterBBSInfo.myNode->name[SYSTEM_NAME_CHARS] = '\0';
		} else if (strcasecmp(name, "league number") == 0) {
			InterBBSInfo.league = atoi(value);
		} else if (strcasecmp(name, "node number") == 0) {
			InterBBSInfo.myNode->nodeNumber = atoi(value);
		} else if (strcasecmp(name, "file inbox") == 0) {
			strncpy(InterBBSInfo.myNode->filebox, value, PATH_MAX);
			InterBBSInfo.myNode->filebox[PATH_MAX] = '\0';
		} else if (strcasecmp(name, "default outbox") == 0) {
			strncpy(InterBBSInfo.defaultFilebox, value, PATH_MAX);
			InterBBSInfo.defaultFilebox[PATH_MAX] = '\0';
		}
	}
	return 1;
}

int select_bbs(int type) {
	int i;
	char buffer[8];

	while (1) {
		if (type == 1) {
			od_printf("\r\nWhich Galaxy do you want to attack...\r\n");
		} else {
			od_printf("\r\nWhich Galaxy do you want to send a message to...\r\n");
		}
		for (i=0;i<InterBBSInfo.otherNodeCount;i++) {
			od_printf(" (%d) %s\r\n", i+1, InterBBSInfo.otherNodes[i]->name);
		}
		od_printf(" (0) Cancel\r\n");
		od_input_str(buffer, 8, '0', '9');
		i = atoi(buffer);
		if (i <= 0) {
			return 256;
		}

		if (InterBBSInfo.otherNodes[i-1]->nodeNumber == InterBBSInfo.myNode->nodeNumber) {
			if (type == 1) {
				od_printf(" Cannot launch an armarda against a member of your own galaxy!\r\n");
			} else {
				od_printf(" Cannot launch an inter galactic message to a member of your own galaxy!\r\n");
			}
			return 256;
		}
		if (i <= InterBBSInfo.otherNodeCount) {
			if (type == 1) {
				od_printf(" Sending armarda to %s!\r\n", InterBBSInfo.otherNodes[i-1]->name);
			} else {
				od_printf(" Sending a message to %s!\r\n", InterBBSInfo.otherNodes[i-1]->name);
			}
			return InterBBSInfo.otherNodes[i-1]->nodeNumber;
		}
	}
}

int select_ibbs_player(int addr, char *player_name) {
	int rc;
	sqlite3_stmt *stmt;
	sqlite3 *db;
	char buffer[8];
	char sqlbuffer[256];
	int count;
	char **names;
	int i;

	rc = sqlite3_open("interbbs.db3", &db);
	
	if (rc) {
		// Error opening the database
		printf("Error opening interbbs database: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		exit(1);
	}
	sqlite3_busy_timeout(db, 5000);
	count = 0;
	names = NULL;

	snprintf(sqlbuffer, 256, "SELECT gamename FROM scores WHERE address = ?;");
	sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);
	sqlite3_bind_int(stmt, 1, addr);
	rc = sqlite3_step(stmt);
	while (rc == SQLITE_ROW) {
		if (names == NULL) {
			names = (char **)malloc(sizeof(char *));
			if (names == NULL) {
				sqlite3_finalize(stmt);
				sqlite3_close(db);
				return -1;
			}
		} else {
			names = (char **)realloc(names, sizeof(char *) * (count + 1));
			if (names == NULL) {
				sqlite3_finalize(stmt);
				sqlite3_close(db);
				return -1;
			}
		}

		names[count] = (char *)malloc(sizeof(char) * 17);
		if (names[count] == NULL) {
			sqlite3_finalize(stmt);
			sqlite3_close(db);
			return -1;
		}
		strncpy(names[count], (const char *)sqlite3_column_text(stmt, 0), 17);

		count++;
		rc = sqlite3_step(stmt);
	}
	sqlite3_finalize(stmt);
	sqlite3_close(db);

	while (1) {
		od_printf("\r\nSelect an empire...\r\n");
		for (i = 0;i<count;i++) {
			od_printf(" (%d) %s\r\n", i+1, names[i]);
		}
		od_printf(" (0) Cancel\r\n");
		od_input_str(buffer, 8, '0', '9');
		i = atoi(buffer);
		if (i == 0) {
			for (i=0;i<count;i++) {
				free(names[i]);
			}
			free(names);
			return -1;
		}
		if (i <= count) {
			strncpy(player_name, names[i-1], 17);
			for (i=0;i<count;i++) {
				free(names[i]);
			}
			free(names);
			return 0;
		}
	}
}

void send_message(player_t *to, player_t *from, char *body)
{
	int rc;
	sqlite3_stmt *stmt;
	sqlite3 *db;
	char sqlbuffer[256];

	rc = sqlite3_open("msgs.db3", &db);
	if (rc) {
		// Error opening the database
        printf("Error opening messages database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
	sqlite3_busy_timeout(db, 5000);
	if (from == NULL) {
		snprintf(sqlbuffer, 256, "INSERT INTO messages (recipient, system, date, seen, body) VALUES(?, ?, ?, ?, ?)");
		sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);
		sqlite3_bind_text(stmt, 1, to->gamename, strlen(to->gamename) + 1, SQLITE_STATIC);
		sqlite3_bind_int(stmt, 2, 1);
		sqlite3_bind_int(stmt, 3, time(NULL));
		sqlite3_bind_int(stmt, 4, 0);
		sqlite3_bind_text(stmt, 5, body, strlen(body) + 1, SQLITE_STATIC);
	} else {
		snprintf(sqlbuffer, 256, "INSERT INTO messages (recipient, 'from', system, date, seen, body) VALUES(?, ?, ?, ?, ?, ?)");
		sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);
		sqlite3_bind_text(stmt, 1, to->gamename, strlen(to->gamename) + 1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, from->gamename, strlen(from->gamename) + 1, SQLITE_STATIC);
		sqlite3_bind_int(stmt, 3, 0);
		sqlite3_bind_int(stmt, 4, time(NULL));
		sqlite3_bind_int(stmt, 5, 0);
		sqlite3_bind_text(stmt, 6, body, strlen(body) + 1, SQLITE_STATIC);
	}
	rc= sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		// Error opening the database
		od_printf("Error saving to messages database: %s\r\n", sqlite3_errmsg(db));
	}
	sqlite3_finalize(stmt);
	sqlite3_close(db);
}

void unseen_msgs(player_t *player) {
	int rc;
	sqlite3_stmt *stmt;
	sqlite3 *db;
	char sqlbuffer[256];
	message_t **msg;
	int msg_count;
	int i;
	struct tm *ptr;

	rc = sqlite3_open("msgs.db3", &db);
	if (rc) {
		// Error opening the database
        printf("Error opening user database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
	sqlite3_busy_timeout(db, 5000);
	snprintf(sqlbuffer, 256, "SELECT * FROM messages WHERE recipient LIKE ?;");
	sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);
	sqlite3_bind_text(stmt, 1, player->gamename, strlen(player->gamename) + 1, SQLITE_STATIC);

	msg = NULL;
	msg_count = 0;

	rc = sqlite3_step(stmt);
	while (rc == SQLITE_ROW) {

		if (msg == NULL) {
			msg = (message_t **)malloc(sizeof(message_t *));
		} else {
			msg = (message_t **)realloc(msg, sizeof(message_t *) * (msg_count + 1));
		}

		msg[msg_count] = (message_t *)malloc(sizeof(message_t));
		msg[msg_count]->id = sqlite3_column_int(stmt, 0);
		strncpy(msg[msg_count]->to, (const char *)sqlite3_column_text(stmt, 1), 17);
		if ((const char *)sqlite3_column_text(stmt, 2) != NULL) {
			strncpy(msg[msg_count]->from, (const char *)sqlite3_column_text(stmt, 2), 17);
		}
		msg[msg_count]->system = sqlite3_column_int(stmt, 3);
		msg[msg_count]->date = sqlite3_column_int(stmt, 4);
		msg[msg_count]->seen = sqlite3_column_int(stmt, 5);
		strncpy(msg[msg_count]->body, (const char *)sqlite3_column_text(stmt, 6), 256);
		msg_count++;

		rc = sqlite3_step(stmt);
	}

	if (rc != SQLITE_DONE) {
		od_printf("Error: %s\n", sqlite3_errmsg(db));
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db);

	// display unseen messages
	od_printf("\r\nDisplaying sub-space messages for %s\r\n", player->gamename);

	for (i=0;i<msg_count;i++) {
		if (msg[i]->seen == 0) {
			if (msg[i]->system == 1) {
				ptr = localtime(&msg[i]->date);
				od_printf("`bright red`System `white`message sent on `bright cyan`%.2d/%.2d/%4d`white`\r\n", ptr->tm_mday, ptr->tm_mon + 1, ptr->tm_year + 1900);
				od_printf("`grey`   %s`white`\r\n", msg[i]->body);
				msg[i]->seen = 1;

			} else {
				ptr = localtime(&msg[i]->date);
				od_printf("`white`Message sent from `bright green`%s`white` on `bright cyan`%.2d/%.2d/%4d`white`\r\n", msg[i]->from, ptr->tm_mday, ptr->tm_mon + 1, ptr->tm_year + 1900);
				od_printf("`grey`   %s`white`\r\n", msg[i]->body);
				msg[i]->seen = 1;
			}
		}
	}

	od_printf("\r\nPress a key to continue\r\n");
	od_get_key(TRUE);

	// update messages
	rc = sqlite3_open("msgs.db3", &db);

	if (rc) {
		// Error opening the database
        printf("Error opening user database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
	sqlite3_busy_timeout(db, 5000);
	for (i=0;i<msg_count;i++) {
		snprintf(sqlbuffer, 256, "UPDATE messages SET seen=? WHERE id = ?");
		sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);
		sqlite3_bind_int(stmt, 1, msg[i]->seen);
		sqlite3_bind_int(stmt, 2, msg[i]->id);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}

	sqlite3_close(db);

	for (i=0;i<msg_count;i++) {
		free(msg[i]);
	}
	free(msg);
}

void unseen_ibbs_msgs(player_t *player) {
	int rc;
	sqlite3_stmt *stmt;
	sqlite3 *db;
	char sqlbuffer[256];
	message_t **msg;
	int msg_count;
	int i;
	struct tm *ptr;
	char *systemname;
	int j;

	rc = sqlite3_open("interbbs.db3", &db);

	if (rc) {
		// Error opening the database
        printf("Error opening interbbs database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
	sqlite3_busy_timeout(db, 5000);
	snprintf(sqlbuffer, 256, "SELECT * FROM messages WHERE recipient LIKE ?;");
	sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);
	sqlite3_bind_text(stmt, 1, player->gamename, strlen(player->gamename) + 1, SQLITE_STATIC);

	msg = NULL;
	msg_count = 0;

	rc = sqlite3_step(stmt);
	while (rc == SQLITE_ROW) {

		if (msg == NULL) {
			msg = (message_t **)malloc(sizeof(message_t *));
		} else {
			msg = (message_t **)realloc(msg, sizeof(message_t *) * (msg_count + 1));
		}

		msg[msg_count] = (message_t *)malloc(sizeof(message_t));
		msg[msg_count]->id = sqlite3_column_int(stmt, 0);
		strncpy(msg[msg_count]->to, (const char *)sqlite3_column_text(stmt, 1), 17);
		strncpy(msg[msg_count]->from, (const char *)sqlite3_column_text(stmt, 2), 17);
		msg[msg_count]->addr = sqlite3_column_int(stmt, 3);
		msg[msg_count]->date = sqlite3_column_int(stmt, 4);
		msg[msg_count]->seen = sqlite3_column_int(stmt, 5);
		strncpy(msg[msg_count]->body, (const char *)sqlite3_column_text(stmt, 6), 256);
		msg_count++;

		rc = sqlite3_step(stmt);
	}

	if (rc != SQLITE_DONE) {
		od_printf("Error: %s\n", sqlite3_errmsg(db));
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db);

	// display unseen messages
	od_printf("\r\nDisplaying inter-galactic messages for %s\r\n", player->gamename);

	for (i=0;i<msg_count;i++) {
		if (msg[i]->seen == 0) {
			ptr = localtime(&msg[i]->date);

			for (j=0;j<InterBBSInfo.otherNodeCount;j++) {
				if (InterBBSInfo.otherNodes[j]->nodeNumber == msg[i]->addr) {
					systemname = InterBBSInfo.otherNodes[j]->name;
					break;
				}
			}

			od_printf("`white`Message sent from `bright yellow`%s`white` by `bright green`%s`white` on `bright cyan`%.2d/%.2d/%4d`white`\r\n", systemname, msg[i]->from, ptr->tm_mday, ptr->tm_mon + 1, ptr->tm_year + 1900);
			od_printf("`grey`   %s`white`\r\n", msg[i]->body);
			msg[i]->seen = 1;
		}
	}

	od_printf("\r\nPress a key to continue\r\n");
	od_get_key(TRUE);

	// update messages
	rc = sqlite3_open("interbbs.db3", &db);

	if (rc) {
		// Error opening the database
        printf("Error opening interbbs database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
	sqlite3_busy_timeout(db, 5000);
	for (i=0;i<msg_count;i++) {
		snprintf(sqlbuffer, 256, "UPDATE messages SET seen=? WHERE id = ?");
		sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);
		sqlite3_bind_int(stmt, 1, msg[i]->seen);
		sqlite3_bind_int(stmt, 2, msg[i]->id);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}

	sqlite3_close(db);

	for (i=0;i<msg_count;i++) {
		free(msg[i]);
	}
	free(msg);
}

int calculate_score(player_t *player) {
	int score;

	score = (player->credits / 1000);
	score += player->troops;
	score += player->generals * 5;
	score += player->fighters * 10;
	score += player->defence_stations * 10;
	score += player->command_ship * 60;
	score += (player->planets_ore + player->planets_food + player->planets_industrial + player->planets_military) * 20;
	score += player->food / 100;
	score += player->population * 10;
	return score;
}



player_t *load_player_gn(char *gamename) {
	player_t *thePlayer;
	int rc;
	sqlite3_stmt *stmt;
	sqlite3 *db;
	char sqlbuffer[256];

	rc = sqlite3_open("users.db3", &db);

	if (rc) {
		// Error opening the database
        printf("Error opening user database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
	sqlite3_busy_timeout(db, 5000);
	snprintf(sqlbuffer, 256, "SELECT * FROM users WHERE gamename LIKE ?;");
	sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);
	sqlite3_bind_text(stmt, 1, gamename, strlen(gamename) + 1, SQLITE_STATIC);

	rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW) {
		thePlayer = (player_t *)malloc(sizeof(player_t));

		if (!thePlayer) {
			printf("OOM\n");
			od_exit(-1, FALSE);
			exit(-1);
		}

		thePlayer->id = sqlite3_column_int(stmt, 0);
		strncpy(thePlayer->gamename, (const char *)sqlite3_column_text(stmt, 2), 17);

		thePlayer->troops = sqlite3_column_int(stmt, 3);
		thePlayer->generals = sqlite3_column_int(stmt, 4);
		thePlayer->fighters = sqlite3_column_int(stmt, 5);
		thePlayer->defence_stations = sqlite3_column_int(stmt, 6);

		thePlayer->population = sqlite3_column_int(stmt, 7);
		thePlayer->food = sqlite3_column_int(stmt, 8);
		thePlayer->credits = sqlite3_column_int(stmt, 9);

		thePlayer->planets_food = sqlite3_column_int(stmt, 10);
		thePlayer->planets_ore = sqlite3_column_int(stmt, 11);
		thePlayer->planets_industrial = sqlite3_column_int(stmt, 12);
		thePlayer->planets_military = sqlite3_column_int(stmt, 13);

		thePlayer->command_ship = sqlite3_column_int(stmt, 14);

		thePlayer->turns_left = sqlite3_column_int(stmt, 15);
		thePlayer->last_played = sqlite3_column_int(stmt, 16);
		thePlayer->spies = sqlite3_column_int(stmt, 17);
		thePlayer->last_score = sqlite3_column_int(stmt, 18);
		thePlayer->total_turns = sqlite3_column_int(stmt, 19);

		sqlite3_finalize(stmt);
		sqlite3_close(db);
	} else {
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		thePlayer = NULL;
	}
	return thePlayer;
}

player_t *load_player(char *bbsname) {
	player_t *thePlayer;
	int rc;
	sqlite3_stmt *stmt;
	sqlite3 *db;
	char sqlbuffer[256];

	rc = sqlite3_open("users.db3", &db);
	if (rc) {
		// Error opening the database
        printf("Error opening user database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
	sqlite3_busy_timeout(db, 5000);
	snprintf(sqlbuffer, 256, "SELECT * FROM users WHERE bbsname LIKE ?;");
	sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);
	sqlite3_bind_text(stmt, 1, bbsname, strlen(bbsname) + 1, SQLITE_STATIC);

	rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW) {
		thePlayer = (player_t *)malloc(sizeof(player_t));

		if (!thePlayer) {
			printf("OOM\n");
			od_exit(-1, FALSE);
			exit(-1);
		}

		thePlayer->id = sqlite3_column_int(stmt, 0);
		strncpy(thePlayer->gamename, (const char *)sqlite3_column_text(stmt, 2), 17);

		thePlayer->troops = sqlite3_column_int(stmt, 3);
		thePlayer->generals = sqlite3_column_int(stmt, 4);
		thePlayer->fighters = sqlite3_column_int(stmt, 5);
		thePlayer->defence_stations = sqlite3_column_int(stmt, 6);

		thePlayer->population = sqlite3_column_int(stmt, 7);
		thePlayer->food = sqlite3_column_int(stmt, 8);
		thePlayer->credits = sqlite3_column_int(stmt, 9);

		thePlayer->planets_food = sqlite3_column_int(stmt, 10);
		thePlayer->planets_ore = sqlite3_column_int(stmt, 11);
		thePlayer->planets_industrial = sqlite3_column_int(stmt, 12);
		thePlayer->planets_military = sqlite3_column_int(stmt, 13);

		thePlayer->command_ship = sqlite3_column_int(stmt, 14);

		thePlayer->turns_left = sqlite3_column_int(stmt, 15);
		thePlayer->last_played = sqlite3_column_int(stmt, 16);
		thePlayer->spies = sqlite3_column_int(stmt, 17);
		thePlayer->last_score = sqlite3_column_int(stmt, 18);
		thePlayer->total_turns = sqlite3_column_int(stmt, 19);
		sqlite3_finalize(stmt);
		sqlite3_close(db);
	} else {
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		thePlayer = NULL;
	}
	return thePlayer;
}

void build_interbbs_scorefile()
{

	FILE *fptr, *fptr2;
	sqlite3 *db;
	char sqlbuffer[256];
	int rc;
	sqlite3_stmt *stmt;
	int score;
	char c;
	ibbsscores_t **scores;
	int player_count;
	ibbsscores_t *ptr;
	int i;
	int j;

	scores = NULL;
	player_count = 0;

	rc = sqlite3_open("users.db3", &db);
	if (rc) {
		// Error opening the database
		printf("Error opening users database: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		exit(1);
	}
	sqlite3_busy_timeout(db, 5000);

	snprintf(sqlbuffer, 256, "SELECT gamename,last_score FROM users;");
	sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);

	rc = sqlite3_step(stmt);
	while (rc == SQLITE_ROW) {
		if (scores == NULL) {
			scores = (ibbsscores_t **)malloc(sizeof(ibbsscores_t *));
		} else {
			scores = (ibbsscores_t **)realloc(scores, sizeof(ibbsscores_t *) * (player_count + 1));
		}

		if (scores == NULL) {
			printf("OOM\n");
			exit (-1);
		}

		scores[player_count] = (ibbsscores_t *)malloc(sizeof(ibbsscores_t));
		strncpy(scores[player_count]->player_name, (char *)sqlite3_column_text(stmt, 0),  17);
		strcpy(scores[player_count]->bbs_name, InterBBSInfo.myNode->name);
		scores[player_count]->score = sqlite3_column_int(stmt, 1);
		player_count++;
		rc = sqlite3_step(stmt);
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db);

	rc = sqlite3_open("interbbs.db3", &db);
	if (rc) {
		// Error opening the database
		printf("Error opening interbbs database: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		exit(1);
	}
	sqlite3_busy_timeout(db, 5000);

	snprintf(sqlbuffer, 256, "SELECT gamename,address,score FROM scores;");
	sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		if (scores == NULL) {
			scores = (ibbsscores_t **)malloc(sizeof(ibbsscores_t *));
		} else {
			scores = (ibbsscores_t **)realloc(scores, sizeof(ibbsscores_t *) * (player_count + 1));
		}

		if (scores == NULL) {
			printf("OOM\n");
			exit (-1);
		}

		scores[player_count] = (ibbsscores_t *)malloc(sizeof(ibbsscores_t));
		strncpy(scores[player_count]->player_name, (char *)sqlite3_column_text(stmt, 0),  17);

		for (i=0;i<InterBBSInfo.otherNodeCount;i++) {
			if (sqlite3_column_int(stmt, 1) == InterBBSInfo.otherNodes[i]->nodeNumber) {
				strncpy(scores[player_count]->bbs_name, InterBBSInfo.otherNodes[i]->name, 40);
				break;
			}
		}

		scores[player_count]->score = sqlite3_column_int(stmt, 2);
		player_count++;
	}
	sqlite3_finalize(stmt);
	sqlite3_close(db);

	for (i=0;i<player_count-1;i++) {
		for (j=0;j<player_count-i-1;j++) {
			if (scores[j]->score < scores[j+1]->score) {
				ptr = scores[j];
				scores[j] = scores[j+1];
				scores[j+1] = ptr;
			}
		}
	}

	fptr = fopen("ibbs_scores.ans", "w");

	if (fptr) {
		fptr2 = fopen("ibbs_score_header.ans", "r");
		if (fptr2) {
			c = fgetc(fptr2);
			while(!feof(fptr2)) {
				fputc(c, fptr);
				c = fgetc(fptr2);
			}
			fclose(fptr2);
		}
		for (i=0;i<player_count;i++) {
			fprintf(fptr, " %-31s %-31s %13d\r\n", scores[i]->player_name, scores[i]->bbs_name, scores[i]->score);
		}

		fptr2 = fopen("ibbs_score_footer.ans", "r");
		if (fptr2) {
			c = fgetc(fptr2);
			while(!feof(fptr2)) {
				fputc(c, fptr);
				c = fgetc(fptr2);
			}
			fclose(fptr2);
		}
		fclose(fptr);	
	}

	fptr = fopen("ibbs_scores.asc", "w");

	if (fptr) {
		fptr2 = fopen("ibbs_score_header.asc", "r");
		if (fptr2) {
			c = fgetc(fptr2);
			while(!feof(fptr2)) {
				fputc(c, fptr);
				c = fgetc(fptr2);
			}
			fclose(fptr2);
		}
		for (i=0;i<player_count;i++) {
			fprintf(fptr, " %-31s %-31s %13d\r\n", scores[i]->player_name, scores[i]->bbs_name, scores[i]->score);
		}

		fptr2 = fopen("ibbs_score_footer.asc", "r");
		if (fptr2) {
			c = fgetc(fptr2);
			while(!feof(fptr2)) {
				fputc(c, fptr);
				c = fgetc(fptr2);
			}
			fclose(fptr2);
		}
		fclose(fptr);	
	}

	for (i=0;i<player_count;i++) {
		free(scores[i]);
	}
	free(scores);
	
}

void build_scorefile()
{
	FILE *fptr, *fptr2;
	sqlite3 *db;
	char sqlbuffer[256];
	int rc;
	sqlite3_stmt *stmt;
	int score;
	char c;

	player_t *player;



	fptr = fopen("scores.ans", "w");

	if (fptr) {
		fptr2 = fopen("score_header.ans", "r");
		if (fptr2) {
			c = fgetc(fptr2);
			while(!feof(fptr2)) {
				fputc(c, fptr);
				c = fgetc(fptr2);
			}

			fclose(fptr2);
		}

		rc = sqlite3_open("users.db3", &db);
		if (rc) {
			// Error opening the database
			printf("Error opening users database: %s\n", sqlite3_errmsg(db));
			sqlite3_close(db);
			exit(1);
		}
		sqlite3_busy_timeout(db, 5000);
		snprintf(sqlbuffer, 256, "SELECT gamename FROM users;");
		sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			player = load_player_gn((char *)sqlite3_column_text(stmt, 0));
			fprintf(fptr, " %-64s %13d\r\n", player->gamename, calculate_score(player));
			free(player);
		}

		sqlite3_finalize(stmt);
		sqlite3_close(db);

		fptr2 = fopen("score_footer.ans", "r");
		if (fptr2) {
			c = fgetc(fptr2);
			while(!feof(fptr2)) {
				fputc(c, fptr);
				c = fgetc(fptr2);
			}

			fclose(fptr2);
		}		
		
		fclose(fptr);


	}

	fptr = fopen("scores.asc", "w");

	if (fptr) {
		fptr2 = fopen("score_header.asc", "r");
		if (fptr2) {
			c = fgetc(fptr2);
			while(!feof(fptr2)) {
				fputc(c, fptr);
				c = fgetc(fptr2);
			}

			fclose(fptr2);
		}

		rc = sqlite3_open("users.db3", &db);
		if (rc) {
			// Error opening the database
			printf("Error opening users database: %s\n", sqlite3_errmsg(db));
			sqlite3_close(db);
			exit(1);
		}
		sqlite3_busy_timeout(db, 5000);
		snprintf(sqlbuffer, 256, "SELECT gamename FROM users;");
		sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			player = load_player_gn((char *)sqlite3_column_text(stmt, 0));
			fprintf(fptr, " %-64s %13d\r\n", player->gamename, calculate_score(player));
			free(player);
		}

		sqlite3_finalize(stmt);
		sqlite3_close(db);
		
		fptr2 = fopen("score_footer.asc", "r");
		if (fptr2) {
			c = fgetc(fptr2);
			while(!feof(fptr2)) {
				fputc(c, fptr);
				c = fgetc(fptr2);
			}

			fclose(fptr2);
		}		
		
		fclose(fptr);
	}	
}

player_t *new_player(char *bbsname) {
	player_t *player;
	char c;
	int rc;
	sqlite3_stmt *stmt;
	sqlite3 *db;
	char sqlbuffer[256];
	player = (player_t *)malloc(sizeof(player_t));

	if (!player) {
		printf("OOM!\n");
		od_exit(-1, FALSE);
		exit(-1);
	}

	player->id = -1;

	strncpy(player->bbsname, bbsname, 256);

	rc = sqlite3_open("users.db3", &db);
	if (rc) {
		// Error opening the database
        printf("Error opening user database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
	sqlite3_busy_timeout(db, 5000);
	od_send_file("instruction.ans");

	while (1) {
		od_printf("\r\n`white`Welcome to `bright blue`Galactic Dynasty`white`!\r\n");
		od_printf("What would you like to name your empire: ");
		od_input_str(player->gamename, 17, 32, 126);

		if (strlen(player->gamename) == 0) {
			free(player);
			return NULL;
		}

		if (strlen(player->gamename) == 1) {
			od_printf("\r\nSorry, that name is too short.\r\n");
			continue;
		}

		snprintf(sqlbuffer, 256, "SELECT * FROM users WHERE gamename LIKE ?;");
		sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);
		sqlite3_bind_text(stmt, 1, player->gamename, strlen(player->gamename) + 1, SQLITE_STATIC);

		rc = sqlite3_step(stmt);
		if (rc == SQLITE_ROW) {
			od_printf("\r\n\r\nSorry, this name is taken.\r\n");
		} else {

			od_printf("\r\n\r\nDoes the mighty empire of `bright green`%s`white` sound ok? (Y/N) ", player->gamename);
			c = od_get_answer("yYnN");
			if (tolower(c) == 'y') {
				break;
			}
		}
		sqlite3_finalize(stmt);
	}


	sqlite3_close(db);

	player->troops = 100;
	player->generals = 0;
	player->fighters = 0;
	player->defence_stations = 0;
	player->spies = 0;

	player->population = 250;
	player->food = 50;
	player->credits = 1000;

	player->planets_food = 1;
	player->planets_ore = 0;
	player->planets_industrial = 0;
	player->planets_military = 0;

	player->command_ship = 0;

	player->turns_left = turns_per_day;
	player->last_played = time(NULL);
	player->last_score = 0;
	player->total_turns = 0;
	return player;
}

void list_empires(player_t *me)
{
	int rc;
	sqlite3_stmt *stmt;
	sqlite3 *db;
	char sqlbuffer[256];

	rc = sqlite3_open("users.db3", &db);
	if (rc) {
		// Error opening the database
        printf("Error opening user database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
	sqlite3_busy_timeout(db, 5000);
	snprintf(sqlbuffer, 256, "SELECT gamename FROM USERS");
	sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);

	rc = sqlite3_step(stmt);
	while (rc == SQLITE_ROW) {
		if (strcmp((const char *)sqlite3_column_text(stmt, 0), me->gamename) != 0) {
			od_printf("%s\r\n", sqlite3_column_text(stmt, 0));
		}
		rc = sqlite3_step(stmt);
	}
	sqlite3_finalize(stmt);
	sqlite3_close(db);
}

void save_player(player_t *player) {
	int rc;
	sqlite3_stmt *stmt;
	sqlite3 *db;
	char sqlbuffer[1024];

	rc = sqlite3_open("users.db3", &db);
	if (rc) {
		// Error opening the database
        printf("Error opening user database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
	sqlite3_busy_timeout(db, 5000);	
	if (player->id == -1) {
			snprintf(sqlbuffer, 1024, "INSERT INTO users (bbsname, gamename, troops, generals, fighters, defence_stations, "
									  "population, food, credits, planets_food, planets_ore, planets_industrial, "
									  "planets_military, command_ship, turns_left, last_played, spies, last_score, total_turns) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,?)");
			sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);
			sqlite3_bind_text(stmt, 1, player->bbsname, strlen(player->bbsname) + 1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, 2, player->gamename, strlen(player->gamename) + 1, SQLITE_STATIC);
			sqlite3_bind_int(stmt, 3, player->troops);
			sqlite3_bind_int(stmt, 4, player->generals);
			sqlite3_bind_int(stmt, 5, player->fighters);
			sqlite3_bind_int(stmt, 6, player->defence_stations);
			sqlite3_bind_int(stmt, 7, player->population);
			sqlite3_bind_int(stmt, 8, player->food);
			sqlite3_bind_int(stmt, 9, player->credits);
			sqlite3_bind_int(stmt, 10, player->planets_food);
			sqlite3_bind_int(stmt, 11, player->planets_ore);
			sqlite3_bind_int(stmt, 12, player->planets_industrial);
			sqlite3_bind_int(stmt, 13, player->planets_military);
			sqlite3_bind_int(stmt, 14, player->command_ship);
			sqlite3_bind_int(stmt, 15, player->turns_left);
			sqlite3_bind_int(stmt, 16, player->last_played);
			sqlite3_bind_int(stmt, 17, player->spies);
			sqlite3_bind_int(stmt, 18, player->last_score);
			sqlite3_bind_int(stmt, 19, player->total_turns);
	} else {
			snprintf(sqlbuffer, 1024, "UPDATE users SET gamename=?,"
													   "troops=?,"
													   "generals=?,"
													   "fighters=?,"
													   "defence_stations=?,"
													   "population=?,"
													   "food=?,"
													   "credits=?,"
													   "planets_food=?, "
													   "planets_ore=?, "
													   "planets_industrial=?, "
													   "planets_military=?,"
													   "command_ship=?,"
													   "turns_left=?,"
													   "last_played=?,"
													   "spies=?, "
													   "last_score=?, "
													   "total_turns=? WHERE id=?;");
			sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);
			sqlite3_bind_text(stmt, 1, player->gamename, strlen(player->gamename) + 1, SQLITE_STATIC);
			sqlite3_bind_int(stmt, 2, player->troops);
			sqlite3_bind_int(stmt, 3, player->generals);
			sqlite3_bind_int(stmt, 4, player->fighters);
			sqlite3_bind_int(stmt, 5, player->defence_stations);
			sqlite3_bind_int(stmt, 6, player->population);
			sqlite3_bind_int(stmt, 7, player->food);
			sqlite3_bind_int(stmt, 8, player->credits);
			sqlite3_bind_int(stmt, 9, player->planets_food);
			sqlite3_bind_int(stmt, 10, player->planets_ore);
			sqlite3_bind_int(stmt, 11, player->planets_industrial);
			sqlite3_bind_int(stmt, 12, player->planets_military);
			sqlite3_bind_int(stmt, 13, player->command_ship);
			sqlite3_bind_int(stmt, 14, player->turns_left);
			sqlite3_bind_int(stmt, 15, player->last_played);
			sqlite3_bind_int(stmt, 16, player->spies);
			sqlite3_bind_int(stmt, 17, player->last_score);
			sqlite3_bind_int(stmt, 18, player->total_turns);
			sqlite3_bind_int(stmt, 19, player->id);
	}
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		// Error opening the database
		od_printf("Error saving to users database: %s\r\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		//od_exit(FALSE, 1);
		return;
	}
	sqlite3_finalize(stmt);
	sqlite3_close(db);

	build_scorefile();
}

int do_interbbs_battle(char *victim, char *attacker, int from, int troops, int generals, int fighters, ibbsmsg_t *msg)
{
	int plunder_people;
	int plunder_credits;
	int plunder_food;
	int attack;
	int defence;
	float victory_chance;
	int battle;
	int enemy_troops;
	int enemy_generals;
	int enemy_defence_stations;
	char bbs_name[40];
	char message[256];
	int i;
	player_t *victim_player = load_player_gn(victim);
	if (victim_player == NULL) {
		return -1;
	}

	memset(bbs_name, 0, 40);

	for (i=0;i<InterBBSInfo.otherNodeCount;i++) {
		if (from == InterBBSInfo.otherNodes[i]->nodeNumber) {
			strncpy(bbs_name, InterBBSInfo.otherNodes[i]->name, 40);
			break;
		}
	}

	if (strlen(bbs_name) == 0) {
		return -1;
	}

	if (victim_player->total_turns <= turns_in_protection) {
		msg->type = 3;
		msg->from = InterBBSInfo.myNode->nodeNumber;
		strcpy(msg->player_name, attacker);
		strcpy(msg->victim_name, victim);

		msg->created = time(NULL);		
		msg->plunder_credits = 0;
		msg->plunder_people = 0;
		msg->plunder_food = 0;
		msg->troops = troops;
		msg->fighters = fighters;
		msg->generals = generals;
		msg->score = 2;
		free(victim_player);		
		return 0;
	}

	// attack soldiers
	attack = (3 * troops + 1 * fighters) +  ((3 * troops + 1 * fighters) * (generals / 100));
	defence = 10 * victim_player->troops;

	// attack defence stations
	attack = attack + ( 1 * troops + 4 * fighters +  ((1 * troops + 4 * fighters) * (generals / 100)));
	defence = defence + (25 * victim_player->defence_stations);

	victory_chance = ((float)attack / ((float)attack + (float)defence)) ;
	battle = rand() % 100 + 1;

	if (battle < (victory_chance * 100)) {
		// victory
		// people
		plunder_people = victim_player->population * 0.10;
		if (plunder_people > 0) {
			victim_player->population -= plunder_people;
			msg->plunder_people = plunder_people;
		}

		// credits
		plunder_credits = victim_player->credits * 0.10;
		if (plunder_credits > 0) {
			victim_player->credits -= plunder_credits;
			msg->plunder_credits = plunder_credits;
		}
		// food
		plunder_food = victim_player->food * 0.10;
		if (plunder_food > 0) {
			victim_player->food -= plunder_food;
			msg->plunder_food = plunder_food;
		}

		if (victory_chance > 0.75) {
			victory_chance = 0.75;
		}

		msg->troops = troops * victory_chance;
		msg->generals = generals * victory_chance;
		msg->fighters = fighters * victory_chance;

		enemy_troops = victim_player->troops * victory_chance;
		enemy_generals = victim_player->generals * victory_chance;
		enemy_defence_stations = victim_player->defence_stations * victory_chance;


		snprintf(message, 256, "%s from %s attacked you and won, you lost %d citizens, %d credits, %d food, %d troops, %d generals, %d defence stations.", attacker, bbs_name,
			plunder_people, plunder_credits, plunder_food, victim_player->troops - enemy_troops, victim_player->generals - enemy_generals, victim_player->defence_stations - enemy_defence_stations);
		msg->score = 1;
	} else {
		// defeat
		if (victory_chance > 0.75) {
			victory_chance = 0.75;
		}

		msg->troops = troops * victory_chance;
		msg->generals = generals * victory_chance;
		msg->fighters = fighters * victory_chance;

		enemy_troops = victim_player->troops * victory_chance;
		enemy_generals = victim_player->generals * victory_chance;
		enemy_defence_stations = victim_player->defence_stations * victory_chance;

		snprintf(message, 256, "%s from %s attacked you and lost, %d troops, %d generals, %d defence stations were destroyed in the attack", attacker, bbs_name, victim_player->troops - enemy_troops, victim_player->generals - enemy_generals, victim_player->defence_stations - enemy_defence_stations);
		msg->score = 0;
	}

	send_message(victim_player, NULL, message);

	msg->type = 3;
	msg->from = InterBBSInfo.myNode->nodeNumber;
	strcpy(msg->player_name, attacker);
	strcpy(msg->victim_name, victim);

	msg->created = time(NULL);

	victim_player->troops = enemy_troops;
	victim_player->generals = enemy_generals;
	victim_player->defence_stations = enemy_defence_stations;

	save_player(victim_player);
	free(victim_player);
	return 0;
}

void do_battle(player_t *victim, player_t *attacker, int troops, int generals, int fighters)
{
	char message[256];
	int attack;
	int defence;

	int battle;

	int plunder_people;
	int plunder_credits;
	int plunder_food;
	int plunder_planet_ore;
	int plunder_planet_industrial;
	int plunder_planet_military;
	int plunder_planet_food;

	int enemy_troops;
	int enemy_generals;
	int enemy_defence_stations;
	float victory_chance;

	// attack soldiers
	attack = (3 * troops + 1 * fighters) +  ((3 * troops + 1 * fighters) * (generals / 100));
	defence = 10 * victim->troops;

	// attack defence stations
	attack = attack + ( 1 * troops + 4 * fighters +  ((1 * troops + 4 * fighters) * (generals / 100)));
	defence = defence + (25 * victim->defence_stations);

	victory_chance = ((float)attack / ((float)attack + (float)defence));
	battle = rand() % 100 + 1;

	if (battle < (victory_chance * 100)) {
		// victory
		od_printf("`white`You are `bright green`victorious`white`, you plundered:\r\n");
		// people
		plunder_people = victim->population * 0.10;
		if (plunder_people > 0) {
			victim->population -= plunder_people;
			attacker->population += plunder_people;
			od_printf("   - %d citizens\r\n", plunder_people);
		}

		// credits
		plunder_credits = victim->credits * 0.10;
		if (plunder_credits > 0) {
			victim->credits -= plunder_credits;
			attacker->credits += plunder_credits;
			od_printf("   - %d credits\r\n", plunder_credits);
		}
		// food
		plunder_food = victim->food * 0.10;
		if (plunder_food > 0) {
			victim->food -= plunder_food;
			attacker->food += plunder_food;
			od_printf("   - %d food\r\n", plunder_food);
		}
		// planets
		plunder_planet_ore = victim->planets_ore * 0.05;
		if (plunder_planet_ore > 0) {
			victim->planets_ore -= plunder_planet_ore;
			attacker->planets_ore += plunder_planet_ore;
			od_printf("   - %d ore planets\r\n", plunder_planet_ore);
		}
		plunder_planet_food = victim->planets_food * 0.05;
		if (plunder_planet_food > 0) {
			victim->planets_food -= plunder_planet_food;
			attacker->planets_food += plunder_planet_food;
			od_printf("   - %d food planets\r\n", plunder_planet_food);
		}
		plunder_planet_industrial = victim->planets_industrial * 0.05;
		if (plunder_planet_industrial > 0) {
			victim->planets_industrial -= plunder_planet_industrial;
			attacker->planets_industrial += plunder_planet_industrial;
			od_printf("   - %d industrial planets\r\n", plunder_planet_industrial);
		}
		plunder_planet_military = victim->planets_military * 0.05;
		if (plunder_planet_military > 0) {
			victim->planets_military -= plunder_planet_military;
			attacker->planets_military += plunder_planet_military;
			od_printf("   - %d soldier planets\r\n", plunder_planet_military);
		}

		if (victory_chance > 0.75) {
			victory_chance = 0.75;
		}

		troops = troops * victory_chance;
		generals = generals * victory_chance;
		fighters = fighters * victory_chance;

		enemy_troops = victim->troops * victory_chance;
		enemy_generals = victim->generals * victory_chance;
		enemy_defence_stations = victim->defence_stations * victory_chance;

		snprintf(message, 256, "%s attacked you and won, you lost %d citizens, %d credits, %d food, %d planets (%d ore, %d industrial, %d soldier, %d food), %d troops, %d generals, %d defence stations.", attacker->gamename,
			plunder_people, plunder_credits, plunder_food, plunder_planet_food + plunder_planet_military + plunder_planet_industrial + plunder_planet_ore, plunder_planet_ore, plunder_planet_industrial
			, plunder_planet_military, plunder_planet_food,  victim->troops - enemy_troops, victim->generals - enemy_generals, victim->defence_stations - enemy_defence_stations);
	} else {
		// defeat
		if (victory_chance > 0.75) {
			victory_chance = 0.75;
		}

		troops = troops * victory_chance;
		generals = generals * victory_chance;
		fighters = fighters * victory_chance;

		enemy_troops = victim->troops * victory_chance;
		enemy_generals = victim->generals * victory_chance;
		enemy_defence_stations = victim->defence_stations * victory_chance;


		od_printf("`white`You are `bright red`defeated`white`.\r\n");
		snprintf(message, 256, "%s attacked you and lost, %d troops, %d generals, %d defence stations were destroyed in the attack", attacker->gamename, victim->troops - enemy_troops, victim->generals - enemy_generals, victim->defence_stations - enemy_defence_stations);

	}

	send_message(victim, NULL, message);

	od_printf(" %d troops, %d generals and %d fighters return home.\r\n", troops, generals, fighters);
	od_printf(" %d enemy troops, %d enemy generals and %d enemy defence stations were destroyed\r\n", victim->troops - enemy_troops, victim->generals - enemy_generals, victim->defence_stations - enemy_defence_stations);

	od_printf("\r\nPress a key to continue\r\n");
	od_get_key(TRUE);

	attacker->troops += troops;
	attacker->generals += generals;
	attacker->fighters += fighters;

	victim->troops = enemy_troops;
	victim->generals = enemy_generals;
	victim->defence_stations = enemy_defence_stations;

}


void perform_maintenance()
{
	ibbsmsg_t msg;
	ibbsmsg_t outboundmsg;
	int i;
	int rc;
	sqlite3_stmt *stmt;
	sqlite3 *db;
	char sqlbuffer[256];
	char **players;
	player_t *player;
	int j;
        int k;
	time_t last_score;
        tIBResult result;
	char message[256];
	// parse all incoming messages
	i = 0;
        k = 0;
	if (interBBSMode == 1) {
		while (1) {
		    result = IBGet(&InterBBSInfo, &msg, sizeof(ibbsmsg_t));

	 	    if (result == eSuccess) {
			switch(msg.type) {
			case 1:
				// add score to database
				rc = sqlite3_open("interbbs.db3", &db);
				if (rc) {
					// Error opening the database
					printf("Error opening interbbs database: %s\n", sqlite3_errmsg(db));
					sqlite3_close(db);
					exit(1);
				}
				sqlite3_busy_timeout(db, 5000);
				snprintf(sqlbuffer, 256, "SELECT id, last FROM scores WHERE gamename=? and address=?");
				sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);
				sqlite3_bind_text(stmt, 1, msg.player_name, strlen(msg.player_name) + 1, SQLITE_STATIC);
				sqlite3_bind_int(stmt, 2, msg.from);

				rc = sqlite3_step(stmt);

				if (rc == SQLITE_ROW) {
					i = sqlite3_column_int(stmt, 0);
					last_score = sqlite3_column_int(stmt, 1);
					sqlite3_finalize(stmt);
					if (last_score < msg.created) {
						snprintf(sqlbuffer, 256, "UPDATE scores SET score=?, last=? WHERE id=?");
						sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);
						sqlite3_bind_int(stmt, 1, msg.score);
						sqlite3_bind_int(stmt, 2, msg.created);
						sqlite3_bind_int(stmt, 3, i);
						sqlite3_step(stmt);
						sqlite3_finalize(stmt);
					}
				} else {
					sqlite3_finalize(stmt);
					snprintf(sqlbuffer, 256, "INSERT INTO scores (address, gamename, score, last) VALUES(?, ?, ?, ?)");
					sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);
					sqlite3_bind_int(stmt, 1, msg.from);
					sqlite3_bind_text(stmt, 2, msg.player_name, strlen(msg.player_name) + 1, SQLITE_STATIC);
					sqlite3_bind_int(stmt, 3, msg.score);
					sqlite3_bind_int(stmt, 4, msg.created);
					sqlite3_step(stmt);
					sqlite3_finalize(stmt);
				}
				sqlite3_close(db);

				break;
			case 2:
				// perform invasion
				if (do_interbbs_battle(msg.victim_name, msg.player_name, msg.from, msg.troops, msg.generals, msg.fighters, &outboundmsg) == 0) {
					IBSend(&InterBBSInfo, msg.from, &outboundmsg, sizeof(ibbsmsg_t));
				}
				break;
			case 3:
				// return troops
				player = load_player_gn(msg.player_name);
				if (player != NULL) {
					player->troops += msg.troops;
					player->generals += msg.generals;
					player->fighters += msg.fighters;

					if (msg.score == 1) {
						player->population += msg.plunder_people;
						player->credits += msg.plunder_credits;
						player->food += msg.plunder_food;
						snprintf(message, 256, "Your armarda returned victorious, %d troops, %d generals and %d fighters returned with %d prisoners, %d credits and %d food.",
							msg.troops, msg.generals, msg.fighters, msg.plunder_people, msg.plunder_credits, msg.plunder_food);

					} else if (msg.score == 0) {
						snprintf(message, 256, "Your armarda returned defeated, %d troops, %d generals and %d fighters returned.",
							msg.troops, msg.generals, msg.fighters);
					} else {
						snprintf(message, 256, "Your armarda encounted galactic protection and all your troops returned disappointed.");
					}
					send_message(player, NULL, message);
					save_player(player);
					free(player);
				}
				break;
			case 4:
				// message
				rc = sqlite3_open("interbbs.db3", &db);
				if (rc) {
					// Error opening the database
					printf("Error opening interbbs database: %s\n", sqlite3_errmsg(db));
					sqlite3_close(db);
					exit(1);
				}
				sqlite3_busy_timeout(db, 5000);
				snprintf(sqlbuffer, 256, "INSERT INTO messages (recipient, 'from', address, date, seen, body) VALUES(?, ?, ?, ?, ?, ?)");
				sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);
				sqlite3_bind_text(stmt, 1, msg.victim_name, strlen(msg.victim_name) + 1, SQLITE_STATIC);
				sqlite3_bind_text(stmt, 2, msg.player_name, strlen(msg.player_name) + 1, SQLITE_STATIC);
				sqlite3_bind_int(stmt, 3, msg.from);
				sqlite3_bind_int(stmt, 4, msg.created);
				sqlite3_bind_int(stmt, 5, 0);
				sqlite3_bind_text(stmt, 6, msg.message, strlen(msg.message) + 1, SQLITE_STATIC);

				sqlite3_step(stmt);
				sqlite3_finalize(stmt);

				sqlite3_close(db);
				break;
			default:
				printf("Unknown message type: %d\n", msg.type);
				break;
			}
			i++;
		    } else if (result == eForwarded) {
			k++;
		    } else {
			break;
		    }
		}

		printf("Parsed %d inbound messages\nForwarded %d messages\n", i, k);


		// send all score messages
		rc = sqlite3_open("users.db3", &db);
		if (rc) {
			// Error opening the database
			printf("Error opening user database: %s\n", sqlite3_errmsg(db));
			sqlite3_close(db);
			exit(1);
		}
		sqlite3_busy_timeout(db, 5000);
		snprintf(sqlbuffer, 256, "SELECT gamename FROM users;");
		sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);

		i = 0;
		rc = sqlite3_step(stmt);
		while (rc == SQLITE_ROW) {
			if (i == 0) {
				players = (char **)malloc(sizeof(char *));
			} else {
				players = (char **)realloc(players, sizeof(char *) * (i + 1));
			}
			players[i] = (char *)malloc(sizeof(char) * 17);
			strcpy(players[i], (char *)sqlite3_column_text(stmt, 0));
			i++;
			rc = sqlite3_step(stmt);
		}

		sqlite3_finalize(stmt);
		sqlite3_close(db);

		for (j=0;j<i;j++) {
			player = load_player_gn(players[j]);
			if (player != NULL) {

				if (player->last_score != calculate_score(player)) {
					memset(&msg, 0, sizeof(ibbsmsg_t));
					msg.type = 1;
					msg.from = InterBBSInfo.myNode->nodeNumber;
					strcpy(msg.player_name, player->gamename);
					msg.score = calculate_score(player);
					msg.created = time(NULL);
					player->last_score = calculate_score(player);
					save_player(player);

					IBSendAll(&InterBBSInfo, &msg, sizeof(ibbsmsg_t));
				}

				free(player);

			}
			free(players[j]);
		}
		if (i > 0) {
			free(players);
		}

		// build global top scores
		build_interbbs_scorefile();
	}
}


void state_of_the_galaxy(player_t *player) {
	od_printf("\r\n`bright blue`============================================================\r\n");
	od_printf(" `white`State of the Empire.               Today's turns left %d\r\n", player->turns_left);
	od_printf("`bright blue`============================================================\r\n");
	od_printf("`white` - Score        : %d\r\n", calculate_score(player));
	od_printf(" - Population   : %d million\r\n", player->population);
	od_printf(" - Food         : %d tonnes\r\n", player->food);
	od_printf(" - Credits      : %d\r\n", player->credits);
	od_printf(" - Troops       : %d\r\n", player->troops);
	od_printf(" - Generals     : %d\r\n", player->generals);
	od_printf(" - Fighters     : %d\r\n", player->fighters);
	od_printf(" - Spies        : %d\r\n", player->spies);
	od_printf(" - Def. Stations: %d\r\n", player->defence_stations);
	od_printf(" - Command Ship : %d%% complete\r\n", player->command_ship);
	od_printf(" - Planets      : %d\r\n", player->planets_food + player->planets_ore + player->planets_military + player->planets_industrial);
	od_printf("   (Ore %d)(Food %d) (Soldier %d) (Industrial %d)\r\n", player->planets_ore, player->planets_food, player->planets_military, player->planets_industrial);
	if (player->total_turns < turns_in_protection) {
		od_printf("`bright yellow`You have %d turns left under protection.\r\n", turns_in_protection - player->total_turns);
	}
	od_printf("`bright blue`============================================================`white`\r\n");
}

player_t *select_victim(player_t *player, char *prompt, int type)
{
	char gamename[17];
	player_t *victim;

	while (1) {
		od_printf("\r\n%s ('?' for a list, ENTER to cancel) ? ", prompt);
		od_input_str(gamename, 17, 32, 126);
		if (strlen(gamename) == 1 && gamename[0] == '?') {
			list_empires(player);
		} else if (strlen(gamename) == 0) {
			return NULL;
		} else {
			victim = load_player_gn(gamename);
			if (victim == NULL) {
				od_printf("\r\nNo such empire!\r\n");
			} else if (victim->id == player->id) {
				od_printf("\r\nYou can't attack yourself!\r\n");
			} else if (victim->total_turns < turns_in_protection && type == 2) {
				od_printf("\r\nSorry, that empire is under protection.\r\n");
			} else {
				return victim;
			}
		}
	}
	return NULL;
}

void game_loop(player_t *player)
{
	int troop_wages;
	int citizen_hunger;
	int total_industrial;
	int done;
	float starvation;
	float loyalty;

	char message[256];
	char buffer[8];
	ibbsmsg_t msg;
	int addr;

	player_t *victim;

	int i;
	int j;
	char c;

	int troops_to_send;
	int generals_to_send;
	int fighters_to_send;

	unseen_msgs(player);

	if (interBBSMode == 1) {
		unseen_ibbs_msgs(player);
	}

	while (player->turns_left) {

		// Diplomatic Stage
		done = 0;
		while (done == 0) {
			od_printf("\r\n`bright cyan`============================================================\r\n");
			od_printf("`white` Diplomatic Relations\r\n", player->credits);
			od_printf("`bright cyan`============================================================`white`\r\n");
			od_printf("  (1) Send a sub-space message\r\n");
			if (interBBSMode == 1) {
				od_printf("  (2) Send an inter-galactic message\r\n");
			}
			od_printf("  (D) Done\r\n");
			od_printf("`bright cyan`============================================================`white`\r\n");
			if (interBBSMode == 1) {
				c = od_get_answer("12dD\r");
			} else {
				c = od_get_answer("1dD\r");
			}
			switch (c) {
				case '1':
					victim = select_victim(player, "Who do you want to message", 1);
					if (victim != NULL) {
						od_printf("Type your message (256 chars max)\r\n");
						od_input_str(message, 256, 32, 126);
						if (strlen(message) > 0) {
							send_message(victim, player, message);
							od_printf("\r\nMessage sent!\r\n");
						} else {
							od_printf("\r\nNot sending an empty message.\r\n");
						}
						free(victim);
					}
					break;
				case '2':
					addr = select_bbs(2);
					if (addr != 256) {
						memset(&msg, 0, sizeof(ibbsmsg_t));
						if (select_ibbs_player(addr, msg.victim_name) == 0) {
							od_printf("Type your message (256 chars max)\r\n");
							od_input_str(msg.message, 256, 32, 126);
							if (strlen(msg.message) > 0) {
								msg.type = 4;
								strcpy(msg.player_name, player->gamename);
								msg.from = InterBBSInfo.myNode->nodeNumber;
								msg.created = time(NULL);
								if (IBSend(&InterBBSInfo, addr, &msg, sizeof(ibbsmsg_t)) != eSuccess) {
									od_printf("\r\nMessage failed to send.\r\n");
								} else {
									od_printf("\r\nMessage sent!\r\n");
								}
							} else {
								od_printf("\r\nNot sending an empty message.\r\n");
							}
						}
					}
					break;
				default:
					done = 1;
					break;
			}
		}
		// State of the Galaxy
		state_of_the_galaxy(player);

		// Troops require money
		troop_wages = player->troops * 10 + player->generals * 15 + player->fighters * 20 + player->defence_stations * 20;
		od_printf("Your military requires `bright yellow`%d`white` credits in wages.\r\n", troop_wages);
		od_printf("Pay them (`bright yellow`%d`white`) : ", (troop_wages < player->credits ? troop_wages : player->credits));

		od_input_str(buffer, 8, '0', '9');

		if (strlen(buffer) == 0) {
			i = (troop_wages < player->credits ? troop_wages : player->credits);
		} else {
			i = atoi(buffer);
		}

		if (i < troop_wages) {
			od_printf("\r\n`bright red`Warning! `white`Your troops will not be impressed!\r\n");
		} else {
			i = troop_wages;
		}

		loyalty = (float)i / (float)troop_wages;
		player->credits -= i;

		// People require food
		citizen_hunger = (player->population / 10) + 1;
		od_printf("Your citizens need `bright yellow`%d`white` tonnes of food.\r\n", citizen_hunger);
		od_printf("Feed them (`bright yellow`%d`white`) : ", (citizen_hunger < player->food ? citizen_hunger : player->food));

		od_input_str(buffer, 8, '0', '9');

		if (strlen(buffer) == 0) {
			i = (citizen_hunger < player->food ? citizen_hunger : player->food);
		} else {
			i = atoi(buffer);
		}

		if (i < citizen_hunger) {
			od_printf("\r\n`bright red`Warning! `white`Your citizens will starve!\r\n");
		} else {
			i = citizen_hunger;
		}

		starvation = (float)i / (float)citizen_hunger;
		player->food -= i;

		done = 0;
		while (done == 0) {
			// do you want to buy anything
			od_printf("`bright green`============================================================\r\n");
			od_printf("`white` Buy Stuff                 Your funds: %d credits\r\n", player->credits);
			od_printf("`bright green`=============================`green`[`white`Price`green`]`bright green`===`green`[`white`You Have`green`]`bright green`===========\r\n");
			od_printf("`white` (1) Troops .....................100    %d\r\n", player->troops);
			od_printf(" (2) Generals ...................500    %d\r\n", player->generals);
			od_printf(" (3) Fighters ..................1000    %d\r\n", player->fighters);
			od_printf(" (4) Defence Stations ..........1000    %d\r\n", player->defence_stations);
			od_printf(" (5) Command Ship Components ..10000    %d%%\r\n", player->command_ship);
			od_printf(" (6) Colonize Planets ..........2000    %d\r\n", player->planets_ore + player->planets_food + player->planets_industrial + player->planets_military);
			od_printf(" (7) Food .......................100    %d\r\n", player->food);
			od_printf(" (8) Spies .....................5000    %d\r\n", player->spies);
			od_printf("\r\n");
			od_printf(" (D) Done\r\n");
			od_printf("`bright green`============================================================`white`\r\n");

			c = od_get_answer("12345678dD\r");
			switch (c) {
				case '1':
					od_printf("How many troops do you want to buy? (MAX `bright yellow`%d`white`) ", player->credits / 100);
					od_input_str(buffer, 8, '0', '9');
					if (strlen(buffer) != 0) {
						i = atoi(buffer);
						if (i * 100 > player->credits) {
							od_printf("\r\n`bright red`You can't afford that many!`white`\r\n");
						} else {
							player->troops += i;
							player->credits -= i * 100;
						}
					}
					break;
				case '2':
					od_printf("How many generals do you want to buy? (MAX `bright yellow`%d`white`) ", player->credits / 500);
					od_input_str(buffer, 8, '0', '9');
					if (strlen(buffer) != 0) {
						i = atoi(buffer);
						if (i * 500 > player->credits) {
							od_printf("\r\n`bright red`You can't afford that many!`white`\r\n");
						} else {
							player->generals += i;
							player->credits -= i * 500;
						}
					}
					break;
				case '3':
					od_printf("How many fighters do you want to buy? (MAX `bright yellow`%d`white`) ", player->credits / 1000);
					od_input_str(buffer, 8, '0', '9');
					if (strlen(buffer) != 0) {
						i = atoi(buffer);
						if (i * 1000 > player->credits) {
							od_printf("\r\n`bright red`You can't afford that many!`white`\r\n");
						} else {
							player->fighters += i;
							player->credits -= i * 1000;
						}
					}
					break;
				case '4':
					od_printf("How many defence stations do you want to buy? (MAX `bright yellow`%d`white`) ", player->credits / 1000);
					od_input_str(buffer, 8, '0', '9');
					if (strlen(buffer) != 0) {
						i = atoi(buffer);
						if (i * 1000 > player->credits) {
							od_printf("\r\n`bright red`You can't afford that many!`white`\r\n");
						} else {
							player->defence_stations += i;
							player->credits -= i * 1000;
						}
					}
					break;
				case '5':
					od_printf("How many command ship components do you want to buy? (MAX `bright yellow`%d`white`) ", (player->credits / 10000 < 10 - player->command_ship ? player->credits / 10000:10 - player->command_ship));
					od_input_str(buffer, 8, '0', '9');
					if (strlen(buffer) != 0) {
						i = atoi(buffer);
						if (i + player->command_ship > 10) {
							od_printf("\r\n`bright red`You can't buy that many!`white`\r\n");
						} else if (i * 10000 > player->credits) {
							od_printf("\r\n`bright red`You can't afford that many!`white`\r\n");
						} else {
							player->command_ship += i;
							player->credits -= i * 10000;
						}
					}
					break;
				case '6':
					od_printf("How many planets do you want to buy? (MAX `bright yellow`%d`white`) ", player->credits / 2000);
					od_input_str(buffer, 8, '0', '9');
					if (strlen(buffer) != 0) {
						i = atoi(buffer);
						if (i * 2000 > player->credits) {
							od_printf("\r\n`bright red`You can't afford that many!`white`\r\n");
						} else {
							od_printf("What kind of planets do you want?\r\n");
							od_printf("  1. Ore\r\n");
							od_printf("  2. Food\r\n");
							od_printf("  3. Soldier\r\n");
							od_printf("  4. Industrial\r\n");
							c = od_get_answer("1234");
							switch (c) {
							case '1':
								player->planets_ore += i;
								player->credits -= i * 2000;
								break;
							case '2':
								player->planets_food += i;
								player->credits -= i * 2000;
								break;
							case '3':
								player->planets_military += i;
								player->credits -= i * 2000;
								break;
							case '4':
								player->planets_industrial += i;
								player->credits -= i * 2000;
								break;
							}
						}
					}
					break;
				case '7':
					od_printf("How much food do you want to buy? (MAX `bright yellow`%d`white`) ", player->credits / 100);
					od_input_str(buffer, 8, '0', '9');
					if (strlen(buffer) != 0) {
						i = atoi(buffer);
						if (i * 100 > player->credits) {
							od_printf("\r\n`bright red`You can't afford that many!`white`\r\n");
						} else {
							player->food += i;
							player->credits -= i * 100;
						}
					}
					break;
				case '8':
					od_printf("How many spies do you want to buy? (MAX `bright yellow`%d`white`) ", player->credits / 5000);
					od_input_str(buffer, 8, '0', '9');
					if (strlen(buffer) != 0) {
						i = atoi(buffer);
						if (i * 5000 > player->credits) {
							od_printf("\r\n`bright red`You can't afford that many!`white`\r\n");
						} else {
							player->spies += i;
							player->credits -= i * 5000;
						}
					}
					break;
				case '\r':
				case 'd':
				case 'D':
					done = 1;
					break;
			}
		}

		// covert operations
		if (player->spies > 0) {
			od_printf("`bright magenta`============================================================\r\n");
			od_printf("`white` Covert Operations                 Spies: %d\r\n", player->spies);
			od_printf("`bright magenta`=============================`magenta`[`white`Price`magenta`]`bright magenta`========================\r\n");
			od_printf("`white` (1) Spy on someone............1000\r\n");
			od_printf(" (D) Done\r\n");
			od_printf("`bright magenta`============================================================`white`\r\n");
			c = od_get_answer("1dD\r");
			switch(tolower(c)) {
			case '1':
				victim = select_victim(player, "Who do you want to attack", 2);
				if (victim != NULL) {
					i = rand() % 100 + 1;
					if (i < 50) {
						od_printf("\r\nYour spy was caught and executed!\r\n");
						player->spies--;
						sprintf(message, "A spy from %s was caught trying to infiltrate your empire!", player->gamename);
						send_message(victim, NULL, message);
					} else {
						od_printf("\r\nYour spy was successful!\r\n");
						state_of_the_galaxy(victim);
					}
					free(victim);
				}
				break;
			case '\r':
			case 'd':
				break;
			}
		}
		// do you want to attack anyone
		od_printf("Do you want to launch an attack? (Y/N) ");
		c = od_get_answer("yYnN\r");

		if (tolower(c) == 'y') {
			if (player->total_turns < turns_in_protection) {
				od_printf("\r\nSorry, you are currently in protection and can not attack\r\n");
			} else {
				victim = select_victim(player, "Who do you want to attack", 2);
				if (victim != NULL) {
					// do attack
					if (player->troops > 0) {
						while (1) {
							od_printf("\r\nSend how many troops? (MAX %d) ", player->troops);
							od_input_str(buffer, 8, '0', '9');
							if (strlen(buffer) > 0) {
								i = atoi(buffer);
								if (i > player->troops) {
									od_printf("\r\nYou don't have that many!\r\n");
								} else if (i > 0) {
									od_printf("\r\nSending %d troops.\r\n", i);
									troops_to_send = i;
									player->troops -= i;
									break;
								} else if (i==0) {
									od_printf("\r\nYou need at least 1 troop!\r\n");
									break;
								}
							}
						}
					} else {
						od_printf("\r\nYou have no troops!\r\n");
						free(victim);
					}
					if (troops_to_send > 0) {
						if (player->generals > 0) {
							while (1) {
								od_printf("\r\nSend how many generals? (MAX %d) ", player->generals);
								od_input_str(buffer, 8, '0', '9');
								if (strlen(buffer) > 0) {
									i = atoi(buffer);
									if (i > player->generals) {
										od_printf("\r\nYou don't have that many!\r\n");
									} else {
										od_printf("\r\nSending %d generals.\r\n", i);
										generals_to_send = i;
										player->generals -= i;
										break;
									}
								}
							}
						} else {
							generals_to_send = 0;
						}
						if (player->fighters > 0) {
							while (1) {
								od_printf("\r\nSend how many fighters? (MAX %d) ", player->fighters);
								od_input_str(buffer, 8, '0', '9');
								if (strlen(buffer) > 0) {
									i = atoi(buffer);
									if (i > player->fighters) {
										od_printf("\r\nYou don't have that many!\r\n");
									} else {
										od_printf("\r\nSending %d fighters.\r\n", i);
										fighters_to_send = i;
										player->fighters -= i;
										break;
									}
								}
							}
						} else {
							fighters_to_send = 0;
						}
						do_battle(victim, player, troops_to_send, generals_to_send, fighters_to_send);
						save_player(victim);
					}
					free(victim);
				}
			}
		}
		if (interBBSMode == 1) {
			od_printf("\r\nDo you want to launch an Inter-Galactic Armarda? (Y/N) ");
			c = od_get_answer("YyNn\r");
			if (tolower(c) == 'y') {
				if (player->total_turns < turns_in_protection) {
					od_printf("\r\nSorry, you are currently under protection and can not attack.\r\n");
				} else {
					addr = select_bbs(1);
					if (addr != 256) {
						if (select_ibbs_player(addr, msg.victim_name) == 0) {
							msg.type = 2;
							msg.from = InterBBSInfo.myNode->nodeNumber;
							strcpy(msg.player_name, player->gamename);
							msg.score = 0;
							msg.plunder_credits = 0;
							msg.plunder_food = 0;
							msg.plunder_people = 0;
							msg.created = time(NULL);
							if (player->troops > 0) {
								while (1) {
									od_printf("\r\nSend how many troops? (MAX %d) ", player->troops);
									od_input_str(buffer, 8, '0', '9');
									if (strlen(buffer) > 0) {
										i = atoi(buffer);
										if (i > player->troops) {
											od_printf("\r\nYou don't have that many!\r\n");
										} else if (i > 0) {
											od_printf("\r\nSending %d troops.\r\n", i);
											msg.troops = i;
											player->troops -= i;
											break;
										} else {
											od_printf("\r\nYou must send at least 1 troop.\r\n");
										}
									}
								}
							} else {
								od_printf("\r\nYou have no troops!\r\n");
							}
							if (msg.troops > 0) {
								if (player->generals > 0) {
									while (1) {
										od_printf("\r\nSend how many generals? (MAX %d) ", player->generals);
										od_input_str(buffer, 8, '0', '9');
										if (strlen(buffer) > 0) {
											i = atoi(buffer);
											if (i > player->generals) {
												od_printf("\r\nYou don't have that many!\r\n");
											} else {
												od_printf("\r\nSending %d generals.\r\n", i);
												msg.generals = i;
												player->generals -= i;
												break;
											}
										}
									}
								} else {
									msg.generals = 0;
								}
								if (player->fighters > 0) {
									while (1) {
										od_printf("\r\nSend how many fighters? (MAX %d) ", player->fighters);
										od_input_str(buffer, 8, '0', '9');
										if (strlen(buffer) > 0) {
											i = atoi(buffer);
											if (i > player->fighters) {
												od_printf("\r\nYou don't have that many!\r\n");
											} else {
												od_printf("\r\nSending %d fighters.\r\n", i);
												msg.fighters = i;
												player->fighters -= i;
												break;
											}
										}
									}
								} else {
									msg.fighters = 0;
								}
								// send message
								if (IBSend(&InterBBSInfo, addr, &msg, sizeof(ibbsmsg_t)) != eSuccess) {
									player->troops += msg.troops;
									player->generals += msg.generals;
									player->fighters += msg.fighters;
									od_printf("Your armarda failed to take off. Your forces have been returned.\r\n");
								}
							}
						}
					}
				}
			}
		}
		od_printf("\r\n\r\n");

		// Productions

		if (player->planets_food > 0) {
			od_printf("Your food planets produced %d food.\r\n", 50 * player->planets_food);

			player->food += 50 * player->planets_food;
		}

		if (player->planets_military > 0) {
			od_printf("Your soldier planets provided %d troops\r\n", player->planets_military * 10);

			player->troops += 10 * player->planets_military;
		}

		if (player->planets_ore > 0) {
			od_printf("Your ore planets mined %d worth of minerals\r\n", player->planets_ore * 1000);

			player->credits += player->planets_ore * 1000;
		}

		if (player->planets_industrial > 0) {

			if (player->planets_industrial * 1500 > player->population * 10) {
				total_industrial = player->population * 10;
			} else {
				total_industrial = player->planets_industrial * 1500;
			}

			od_printf("Your industrial planets produced %d worth of goods\r\n", total_industrial);

			player->credits += total_industrial;
		}
		od_printf("Taxes produce %d credits\r\n", player->population * 23);

		player->credits += player->population * 23;

		// Population Changes
		if (starvation < 1) {
			od_printf("%d citizens died from starvation\r\n", (int)(player->population - ((float)player->population * starvation)));
			player->population -= player->population - (player->population * starvation);
		} else {
			od_printf("%d new citizens join the empire\r\n", (int)((float)player->population * 0.05));
			player->population += player->population * 0.05;
		}

		if (loyalty < 1) {
			od_printf("%d troops fled the empire\r\n", player->troops - (player->troops * loyalty));
			player->troops -= player->troops - (player->troops * loyalty);
		}


		// loop
		player->turns_left--;
		player->total_turns++;
		save_player(player);

		if (player->turns_left > 0) {
			od_printf("\r\nContinue ? (Y/N) ");
			c = od_get_answer("YyNn\r");
			if (tolower(c) == 'n') {
				od_printf("\r\n");
				break;
			}
		}

	}
	if (player->turns_left == 0) {
		od_printf("You're out of turns for today, please come back tomorrow.\r\n");
		od_printf("\r\nPress a key to continue\r\n");
		od_get_key(TRUE);
	}

	// show scores
	if (!od_send_file("scores.ans")) {
		od_printf("No score file.\r\n");
	}
	od_printf("\r\nPress a key to continue\r\n");
	od_get_key(TRUE);

}

#if _MSC_VER
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,LPSTR lpszCmdLine,int nCmdShow)
{
#else
int main(int argc, char **argv)
{
#endif
	player_t *player;
	char bbsname[256];
	time_t timenow;
	struct tm today_tm;
	struct tm last_tm;
	struct tm *ptr;
	char c;
	int i;
	struct stat s;

	srand(time(NULL));

	if (stat("users.db3", &s) != 0) {
#ifdef _MSC_VER
		MessageBox(0, "Unable to find users.db3, have you run reset.bat?\nAre you running from the Galactic Dynasty directory?\n", "Error", 0);
#else
		fprintf(stderr, "Unable to find users.db3, have you run reset.sh?\nAre you running from the Galactic Dynasty directory?\n");
#endif
		exit(0);
	}



	turns_per_day = TURNS_PER_DAY;
	turns_in_protection = TURNS_IN_PROTECTION;

	InterBBSInfo.myNode = (tOtherNode *)malloc(sizeof(tOtherNode));
	if (InterBBSInfo.myNode == NULL) {
		fprintf(stderr, "Out of memory!\n");
		exit(-1);
	}

	if (ini_parse("galactic.ini", handler, NULL) <0) {
		fprintf(stderr, "Unable to load galactic.ini");
	}

	if (interBBSMode == 1) {
		if (IBReadConfig(&InterBBSInfo, "BBS.CFG")!= eSuccess) {
			interBBSMode = 0;
		}
	}

#if _MSC_VER
	if (strcasecmp(lpszCmdLine, "maintenance") == 0) {
		perform_maintenance();
		return 0;
	}
	od_parse_cmd_line(lpszCmdLine);
#else
	if (argc > 1 && strcasecmp(argv[1], "maintenance") == 0) {
		perform_maintenance();
		return 0;
	}

	od_parse_cmd_line(argc, argv);
#endif

	od_send_file("logo.ans");
	od_get_key(TRUE);

	snprintf(bbsname, 255, "%s!%s", od_control_get()->user_name, od_control_get()->user_handle);

	player = load_player(bbsname);
	if (player == NULL) {
		player = new_player(bbsname);
		if (player == NULL) {
			od_exit(0, FALSE);
			exit(0);
		} else {
			save_player(player);
			player = load_player(bbsname);
		}
	}

	ptr = localtime(&player->last_played);
	memcpy(&last_tm, ptr, sizeof(struct tm));
	timenow = time(NULL);
	ptr = localtime(&timenow);
	memcpy(&today_tm, ptr, sizeof(struct tm));

	if (today_tm.tm_mday != last_tm.tm_mday) {
		player->turns_left = turns_per_day;
	}
	player->last_played = timenow;

	do {
		od_printf("\r\n`white`Game Menu\r\n");
		od_printf("`red`--------------------------------------\r\n");
		od_printf(" `white`(`bright yellow`1`white`) Play Game\r\n");
		od_printf(" `white`(`bright yellow`2`white`) See Status\r\n");
		od_printf(" `white`(`bright yellow`3`white`) See Scores\r\n");
		if (interBBSMode == 1) {
			od_printf(" `white`(`bright yellow`4`white`) See InterBBS Nodes\r\n");
			od_printf(" `white`(`bright yellow`5`white`) See InterBBS Scores\r\n");
		}
		od_printf(" `white`(`bright yellow`Q`white`) Exit Game\r\n");
		od_printf("`red`--------------------------------------`white`\r\n");
		od_printf("Your Choice? ");
		if (interBBSMode == 1) {
			c = od_get_answer("12345qQ");
		} else {
			c = od_get_answer("123qQ");
		}
		od_printf("\r\n");
		switch (c) {
		case '1':
			game_loop(player);
			break;
		case '2':
			state_of_the_galaxy(player);
			od_printf("\r\nPress a key to continue\r\n");
			od_get_key(TRUE);
			break;
		case '3':
			od_send_file("scores.ans");
			od_printf("\r\nPress a key to continue\r\n");
			od_get_key(TRUE);
			break;
		case '4':
			for (i=0;i<InterBBSInfo.otherNodeCount;i++) {
				od_printf("`bright green`%s`white`\r\n", InterBBSInfo.otherNodes[i]->name);
			}
			od_printf("\r\nPress a key to continue\r\n");
			od_get_key(TRUE);
			break;
		case '5':
			od_send_file("ibbs_scores.ans");
			od_printf("\r\nPress a key to continue\r\n");
			od_get_key(TRUE);
			break;
		default:
			break;
		}
	} while (tolower(c) != 'q');
	free(player);
	od_exit(0, FALSE);
}
