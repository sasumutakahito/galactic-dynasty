#include <OpenDoor.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sqlite3.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/stat.h>
#include <stdarg.h>

#include <lua.h>
#include <lauxlib.h>

#include "interbbs2.h"
#include "inih/ini.h"

#define TURNS_PER_DAY 5
#define TURNS_IN_PROTECTION 0
#if _MSC_VER
#include <winsock2.h>
#include <windows.h>
#define snprintf _snprintf
#define strcasecmp _stricmp
#else
#include <arpa/inet.h>
#endif

int turns_per_day;
int turns_in_protection;
int full;
uint32_t game_id;
char *log_path;

tIBInfo InterBBSInfo;
int interBBSMode;

typedef struct player {
	int id;
	char bbsname[256];
	char gamename[17];

	uint32_t troops;
	uint32_t generals;
	uint32_t fighters;
	uint32_t defence_stations;
	uint32_t spies;

	uint32_t population;
	uint32_t food;
	uint32_t credits;

	uint32_t planets_food;
	uint32_t planets_ore;
	uint32_t planets_industrial;
	uint32_t planets_military;
	uint32_t planets_urban;

	uint32_t command_ship;

	uint32_t turns_left;
	time_t last_played;
	uint32_t last_score;
	uint32_t total_turns;
	int64_t bank_balance;
} player_t;

player_t *gPlayer;

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
	uint32_t score;
	uint32_t troops;
	uint32_t generals;
	uint32_t fighters;
	uint32_t plunder_credits;
	uint32_t plunder_food;
	uint32_t plunder_people;
	char message[256];
	uint32_t created;
	uint32_t turns_per_day;
	uint32_t turns_in_protection;
	uint32_t game_id;
} __attribute__((packed)) ibbsmsg_t;

typedef struct ibbsscore {
	char player_name[17];
	char bbs_name[40];
	int score;
} ibbsscores_t;

void msg2ne(ibbsmsg_t *msg) {
	msg->type = htonl(msg->type);
	msg->from = htonl(msg->from);
	msg->score = htonl(msg->score);
	msg->troops = htonl(msg->troops);
	msg->generals = htonl(msg->generals);
	msg->fighters = htonl(msg->fighters);
	msg->plunder_credits = htonl(msg->plunder_credits);
	msg->plunder_food = htonl(msg->plunder_food);
	msg->plunder_people = htonl(msg->plunder_people);
	msg->created = htonl(msg->created);
	msg->turns_per_day = htonl(msg->turns_per_day);
	msg->turns_in_protection = htonl(msg->turns_in_protection);
	msg->game_id = htonl(msg->game_id);
}

void msg2he(ibbsmsg_t *msg) {
	msg->type = ntohl(msg->type);
	msg->from = ntohl(msg->from);
	msg->score = ntohl(msg->score);
	msg->troops = ntohl(msg->troops);
	msg->generals = ntohl(msg->generals);
	msg->fighters = ntohl(msg->fighters);
	msg->plunder_credits = ntohl(msg->plunder_credits);
	msg->plunder_food = ntohl(msg->plunder_food);
	msg->plunder_people = ntohl(msg->plunder_people);
	msg->created = ntohl(msg->created);
	msg->turns_per_day = ntohl(msg->turns_per_day);
	msg->turns_in_protection = ntohl(msg->turns_in_protection);
	msg->game_id = ntohl(msg->game_id);
}

void dolog(char *fmt, ...) {
	char buffer[PATH_MAX];
	struct tm *time_now;
	time_t timen;
	FILE *logfptr;

	timen = time(NULL);

	time_now = localtime(&timen);
	if (log_path != NULL) {
		snprintf(buffer, PATH_MAX, "%s%s%04d%02d%02d.log", log_path, PATH_SEP, time_now->tm_year + 1900, time_now->tm_mon + 1, time_now->tm_mday);
	} else {
		snprintf(buffer, PATH_MAX, "%04d%02d%02d.log", time_now->tm_year + 1900, time_now->tm_mon + 1, time_now->tm_mday);
	}
	logfptr = fopen(buffer, "a");
    if (!logfptr) {
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buffer, 512, fmt, ap);
	va_end(ap);

	fprintf(logfptr, "%02d:%02d:%02d %s\n", time_now->tm_hour, time_now->tm_min, time_now->tm_sec, buffer);

	fclose(logfptr);	
}

static int handler(void* user, const char* section, const char* name,
                   const char* value)
{
	if (strcasecmp(section, "main") == 0) {
		if (strcasecmp(name, "turns per day") == 0) {
			turns_per_day = atoi(value);
		} else if (strcasecmp(name, "turns in protection") == 0) {
			turns_in_protection = atoi(value);
		} else if (strcasecmp(name, "log path") == 0) {
			log_path = strdup(value);
#ifdef _MSC_VER
			if (log_path[strlen(log_path) - 1] == '\\') {
#else
			if (log_path[strlen(log_path) - 1] == '/') {
#endif				
				log_path[strlen(log_path) - 1] = '\0';
			}
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

int lua_getTroops(lua_State *L) {
	lua_pushnumber(L, gPlayer->troops);
	return 1;
}

int lua_getGenerals(lua_State *L) {
	lua_pushnumber(L, gPlayer->generals);
	return 1;
}

int lua_getFighters(lua_State *L) {
	lua_pushnumber(L, gPlayer->fighters);
	return 1;
}

int lua_getDefenceStations(lua_State *L) {
	lua_pushnumber(L, gPlayer->defence_stations);
	return 1;
}

int lua_getSpies(lua_State *L) {
	lua_pushnumber(L, gPlayer->spies);
	return 1;
}

int lua_getPopulation(lua_State *L) {
	lua_pushnumber(L, gPlayer->population);
	return 1;
}

int lua_getFood(lua_State *L) {
	lua_pushnumber(L, gPlayer->food);
	return 1;
}

int lua_getCredits(lua_State *L) {
	lua_pushnumber(L, gPlayer->credits);
	return 1;
}

int lua_setTroops(lua_State *L) {
	gPlayer->troops = lua_tonumber(L, -1);
	return 0;
}

int lua_setGenerals(lua_State *L) {
	gPlayer->generals = lua_tonumber(L, -1);
	return 0;
}

int lua_setFighters(lua_State *L) {
	gPlayer->fighters = lua_tonumber(L, -1);
	return 0;
}

int lua_setDefenceStations(lua_State *L) {
	gPlayer->defence_stations = lua_tonumber(L, -1);
	return 0;
}

int lua_setSpies(lua_State *L) {
	gPlayer->spies = lua_tonumber(L, -1);
	return 0;
}

int lua_setPopulation(lua_State *L) {
	gPlayer->population = lua_tonumber(L, -1);
	return 0;
}

int lua_setFood(lua_State *L) {
	gPlayer->food = lua_tonumber(L, -1);
	return 0;
}

int lua_setCredits(lua_State *L) {
	gPlayer->credits = lua_tonumber(L, -1);
	return 0;
}

int lua_printYellow(lua_State *L) {
	od_printf("`bright yellow`%s`white`\r\n", (char *)lua_tostring(L, -1));
	return 0;
}

int lua_printGreen(lua_State *L) {
	od_printf("`bright green`%s`white`\r\n", (char *)lua_tostring(L, -1));
	return 0;
}

void lua_push_cfunctions(lua_State *L);

void do_lua_script(char *script) {
        lua_State *L;
        char buffer[PATH_MAX];

        if (script == NULL) {
                return;
        }

        snprintf(buffer, PATH_MAX, "%s.lua", script);

        L = luaL_newstate();
        luaL_openlibs(L);
        lua_push_cfunctions(L);
        if (luaL_dofile(L, buffer)) {
			od_printf("`bright red`%s`white`\r\n", lua_tostring(L, -1));
		}
        lua_close(L);
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

uint32_t calculate_score(player_t *player) {
	uint32_t score;

	score = (player->credits / 1000);
	score += player->troops;
	score += player->generals * 5;
	score += player->fighters * 10;
	score += player->defence_stations * 10;
	score += player->command_ship * 60;
	score += (player->planets_ore + player->planets_food + player->planets_industrial + player->planets_military) * 20;
	score += player->food / 100;
	score += player->population * 10;
	score += (player->bank_balance / 1000);
	
	score /= 100;
	
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
		thePlayer->planets_urban = sqlite3_column_int(stmt, 14);
		thePlayer->command_ship = sqlite3_column_int(stmt, 15);

		thePlayer->turns_left = sqlite3_column_int(stmt, 16);
		thePlayer->last_played = sqlite3_column_int(stmt, 17);
		thePlayer->spies = sqlite3_column_int(stmt, 18);
		thePlayer->last_score = sqlite3_column_int(stmt, 19);
		thePlayer->total_turns = sqlite3_column_int(stmt, 20);
		thePlayer->bank_balance = sqlite3_column_int(stmt, 21);
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
		thePlayer->planets_urban = sqlite3_column_int(stmt, 14);
		thePlayer->command_ship = sqlite3_column_int(stmt, 15);

		thePlayer->turns_left = sqlite3_column_int(stmt, 16);
		thePlayer->last_played = sqlite3_column_int(stmt, 17);
		thePlayer->spies = sqlite3_column_int(stmt, 18);
		thePlayer->last_score = sqlite3_column_int(stmt, 19);
		thePlayer->total_turns = sqlite3_column_int(stmt, 20);
		thePlayer->bank_balance = sqlite3_column_int(stmt, 21);
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
			fprintf(fptr, " %-31.31s %-31.31s %13u\r\n", scores[i]->player_name, scores[i]->bbs_name, scores[i]->score);
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
			fprintf(fptr, " %-31.31s %-31.31s %13u\r\n", scores[i]->player_name, scores[i]->bbs_name, scores[i]->score);
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
			fprintf(fptr, " %-64.64s %13u\r\n", player->gamename, calculate_score(player));
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
			fprintf(fptr, " %-64.64s %13u\r\n", player->gamename, calculate_score(player));
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

	player->planets_food = 3;
	player->planets_ore = 0;
	player->planets_industrial = 0;
	player->planets_military = 0;
	player->planets_urban = 6;
	player->command_ship = 0;

	player->turns_left = turns_per_day;
	player->last_played = time(NULL);
	player->last_score = 0;
	player->total_turns = 0;
	player->bank_balance = 0;
	
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
									  "planets_military, command_ship, turns_left, last_played, spies, last_score, total_turns, planets_urban, bank_balance) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
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
			sqlite3_bind_int(stmt, 20, player->planets_urban);
			sqlite3_bind_int(stmt, 21, player->bank_balance);
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
													   "total_turns=?, planets_urban=?, bank_balance = ? WHERE id=?;");
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
			sqlite3_bind_int(stmt, 19, player->planets_urban);
			sqlite3_bind_int(stmt, 20, player->bank_balance);
			sqlite3_bind_int(stmt, 21, player->id);
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

int do_interbbs_battle(char *victim, char *attacker, int from, uint32_t troops, uint32_t generals, uint32_t fighters, ibbsmsg_t *msg)
{
	uint32_t plunder_people;
	uint32_t plunder_credits;
	uint32_t plunder_food;
	uint32_t attack;
	uint32_t defence;
	float victory_chance;
	int battle;
	uint32_t enemy_troops;
	uint32_t enemy_generals;
	uint32_t enemy_defence_stations;
	char bbs_name[40];
	char message[256];
	int i;
	int dtroops;
	int dgenerals;
	int dfighters;
	int difference;
	
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
		dolog("InterBBS Battle: Empire name mismatch");
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

		difference = rand() % 5 + 1;
		dtroops = (int)troops - (int)((float)troops * ((float)difference / 100.f));
		dgenerals = (int)generals - (int)((float)generals * ((float)difference / 100.f));
		dfighters = (int)fighters - (int)((float)fighters * ((float)difference / 100.f));
		
		if (dtroops > victim_player->troops) dtroops = victim_player->troops;
		if (dgenerals > victim_player->generals) dgenerals = victim_player->generals;
		if (dfighters > victim_player->defence_stations) dfighters = victim_player->defence_stations;			
		
		enemy_troops = dtroops;
		enemy_generals = dgenerals;
		enemy_defence_stations = dfighters;
		
		
		dtroops = (int)troops - ((int)victim_player->troops - (int)((float)victim_player->troops * ((float)difference / 100.f)));
		dgenerals = (int)generals - ((int)victim_player->generals - (int)((float)victim_player->generals * ((float)difference / 100.f)));
		dfighters = (int)fighters - ((int)victim_player->fighters - (int)((float)victim_player->defence_stations * ((float)difference / 100.f)));

		
		if (dtroops < 0) dtroops = 0;
		if (dgenerals < 0) dgenerals = 0;
		if (dfighters < 0) dfighters = 0;
		
		msg->troops = dtroops;
		msg->generals = dgenerals;
		msg->fighters = dfighters;
				
/*
		msg->troops = troops * victory_chance;
		msg->generals = generals * victory_chance;
		msg->fighters = fighters * victory_chance;

		enemy_troops = (uint32_t)((float)victim_player->troops * (1.f - (float)victory_chance));
		enemy_generals = (uint32_t)((float)victim_player->generals * (1.f - (float)victory_chance));
		enemy_defence_stations = (uint32_t)((float)victim_player->defence_stations * (1.f - (float)victory_chance));
*/

		snprintf(message, 256, "%s from %s attacked you and won, you lost %u citizens, %u credits, %u food, %u troops, %u generals, %u defence stations.", attacker, bbs_name,
			plunder_people, plunder_credits, plunder_food, enemy_troops, enemy_generals, enemy_defence_stations);
		
		victim_player->troops -= enemy_troops;
		victim_player->generals -= enemy_generals;
		victim_player->defence_stations -= enemy_defence_stations;
		msg->score = 1;
	} else {
		// defeat
		difference = rand() % 5 + 1;
		dtroops = (int)troops - (int)((float)troops * ((float)difference / 100.f));
		dgenerals = (int)generals - (int)((float)generals * ((float)difference / 100.f));
		dfighters = (int)fighters - (int)((float)fighters * ((float)difference / 100.f));
		
		if (dtroops > victim_player->troops) dtroops = victim_player->troops;
		if (dgenerals > victim_player->generals) dgenerals = victim_player->generals;
		if (dfighters > victim_player->defence_stations) dfighters = victim_player->defence_stations;		
		
		enemy_troops = dtroops;
		enemy_generals = dgenerals;
		enemy_defence_stations = dfighters;
		
		
		dtroops = (int)troops - ((int)victim_player->troops - (int)((float)victim_player->troops * ((float)difference / 100.f)));
		dgenerals = (int)generals - ((int)victim_player->generals - (int)((float)victim_player->generals * ((float)difference / 100.f)));
		dfighters = (int)fighters - ((int)victim_player->fighters - (int)((float)victim_player->defence_stations * ((float)difference / 100.f)));
		

		
		if (dtroops < 0) dtroops = 0;
		if (dgenerals < 0) dgenerals = 0;
		if (dfighters < 0) dfighters = 0;
		
		msg->troops = dtroops;
		msg->generals = dgenerals;
		msg->fighters = dfighters;

		snprintf(message, 256, "%s from %s attacked you and lost, %u troops, %u generals, %u defence stations were destroyed in the attack.", attacker, bbs_name, enemy_troops, enemy_generals, enemy_defence_stations);
		victim_player->troops -= enemy_troops;
		victim_player->generals -= enemy_generals;
		victim_player->defence_stations -= enemy_defence_stations;
		msg->score = 0;
	}

	dolog("InterBBS Battle: Attack %u, Defence %u, Victory Chance %f, Battle %d", attack, defence, victory_chance, battle);

	send_message(victim_player, NULL, message);

	msg->type = 3;
	msg->from = InterBBSInfo.myNode->nodeNumber;
	strcpy(msg->player_name, attacker);
	strcpy(msg->victim_name, victim);

	msg->created = time(NULL);

	save_player(victim_player);
	free(victim_player);
	return 0;
}

void do_battle(player_t *victim, player_t *attacker, int troops, int generals, int fighters)
{
	char message[256];
	uint32_t attack;
	uint32_t defence;

	int battle;
	int difference;
	
	uint32_t plunder_people;
	uint32_t plunder_credits;
	uint32_t plunder_food;
	uint32_t plunder_planet_ore;
	uint32_t plunder_planet_industrial;
	uint32_t plunder_planet_military;
	uint32_t plunder_planet_food;

	uint32_t enemy_troops;
	uint32_t enemy_generals;
	uint32_t enemy_defence_stations;
	int dtroops;
	int dgenerals;
	int dfighters;
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
			od_printf("   - %u citizens\r\n", plunder_people);
		}

		// credits
		plunder_credits = victim->credits * 0.10;
		if (plunder_credits > 0) {
			victim->credits -= plunder_credits;
			attacker->credits += plunder_credits;
			od_printf("   - %u credits\r\n", plunder_credits);
		}
		// food
		plunder_food = victim->food * 0.10;
		if (plunder_food > 0) {
			victim->food -= plunder_food;
			attacker->food += plunder_food;
			od_printf("   - %u food\r\n", plunder_food);
		}
		// planets
		plunder_planet_ore = victim->planets_ore * 0.05;
		if (plunder_planet_ore > 0) {
			victim->planets_ore -= plunder_planet_ore;
			attacker->planets_ore += plunder_planet_ore;
			od_printf("   - %u ore planets\r\n", plunder_planet_ore);
		}
		plunder_planet_food = victim->planets_food * 0.05;
		if (plunder_planet_food > 0) {
			victim->planets_food -= plunder_planet_food;
			attacker->planets_food += plunder_planet_food;
			od_printf("   - %u food planets\r\n", plunder_planet_food);
		}
		plunder_planet_industrial = victim->planets_industrial * 0.05;
		if (plunder_planet_industrial > 0) {
			victim->planets_industrial -= plunder_planet_industrial;
			attacker->planets_industrial += plunder_planet_industrial;
			od_printf("   - %u industrial planets\r\n", plunder_planet_industrial);
		}
		plunder_planet_military = victim->planets_military * 0.05;
		if (plunder_planet_military > 0) {
			victim->planets_military -= plunder_planet_military;
			attacker->planets_military += plunder_planet_military;
			od_printf("   - %u soldier planets\r\n", plunder_planet_military);
		}
		
		difference = rand() % 5 + 1;
		dtroops = (int)troops - (int)((float)troops * ((float)difference / 100.f));
		dgenerals = (int)generals - (int)((float)generals * ((float)difference / 100.f));
		dfighters = (int)fighters - (int)((float)fighters * ((float)difference / 100.f));
		
		if (dtroops > victim->troops) dtroops = victim->troops;
		if (dgenerals > victim->generals) dgenerals = victim->generals;
		if (dfighters > victim->defence_stations) dfighters = victim->defence_stations;		
		
		enemy_troops = dtroops;
		enemy_generals = dgenerals;
		enemy_defence_stations = dfighters;
		
		
		dtroops = (int)troops - ((int)victim->troops - (int)((float)victim->troops * ((float)difference / 100.f)));
		dgenerals = (int)generals - ((int)victim->generals - (int)((float)victim->generals * ((float)difference / 100.f)));
		dfighters = (int)fighters - ((int)victim->fighters - (int)((float)victim->defence_stations * ((float)difference / 100.f)));
		
		if (dtroops < 0) dtroops = 0;
		if (dgenerals < 0) dgenerals = 0;
		if (dfighters < 0) dfighters = 0;
		
		troops = dtroops;
		generals = dgenerals;
		fighters = dfighters;
		
		
/*
		if (victory_chance > 0.75) {
			victory_chance = 0.75;
		}
		
		
		
		troops = troops * victory_chance;
		generals = generals * victory_chance;
		fighters = fighters * victory_chance;

		enemy_troops = victim->troops * victory_chance;
		enemy_generals = victim->generals * victory_chance;
		enemy_defence_stations = victim->defence_stations * victory_chance;
*/
		snprintf(message, 256, "%s attacked you and won, you lost %u citizens, %u credits, %u food, %u planets (%u ore, %u industrial, %u soldier, %u food), %u troops, %u generals, %u defence stations.", attacker->gamename,
			plunder_people, plunder_credits, plunder_food, plunder_planet_food + plunder_planet_military + plunder_planet_industrial + plunder_planet_ore, plunder_planet_ore, plunder_planet_industrial
			, plunder_planet_military, plunder_planet_food,  victim->troops - enemy_troops, victim->generals - enemy_generals, victim->defence_stations - enemy_defence_stations);
	} else {
		// defeat
		
		difference = rand() % 5 +1;
		dtroops = (int)troops - (int)((float)troops * ((float)difference / 100.f));
		dgenerals = (int)generals - (int)((float)generals * ((float)difference / 100.f));
		dfighters = (int)fighters - (int)((float)fighters * ((float)difference / 100.f));
		
		if (dtroops > victim->troops) dtroops = victim->troops;
		if (dgenerals > victim->generals) dgenerals = victim->generals;
		if (dfighters > victim->defence_stations) dfighters = victim->defence_stations;		
		
		enemy_troops = dtroops;
		enemy_generals = dgenerals;
		enemy_defence_stations = dfighters;
		
		dtroops = (int)troops - ((int)victim->troops - (int)((float)victim->troops * ((float)difference / 100.f)));
		dgenerals = (int)generals - ((int)victim->generals - (int)((float)victim->generals * ((float)difference / 100.f)));
		dfighters = (int)fighters - ((int)victim->fighters - (int)((float)victim->defence_stations * ((float)difference / 100.f)));
	
		
		if (dtroops < 0) dtroops = 0;
		if (dgenerals < 0) dgenerals = 0;
		if (dfighters < 0) dfighters = 0;
		
		troops = dtroops;
		generals = dgenerals;
		fighters = dfighters;
/*
		if (victory_chance > 0.75) {
			victory_chance = 0.75;
		}

		troops = troops * victory_chance;
		generals = generals * victory_chance;
		fighters = fighters * victory_chance;

		enemy_troops = victim->troops * victory_chance;
		enemy_generals = victim->generals * victory_chance;
		enemy_defence_stations = victim->defence_stations * victory_chance;
*/
		od_printf("`white`You are `bright red`defeated`white`.\r\n");
		snprintf(message, 256, "%s attacked you and lost, %u troops, %u generals, %u defence stations were destroyed in the attack", attacker->gamename, victim->troops - enemy_troops, victim->generals - enemy_generals, victim->defence_stations - enemy_defence_stations);

	}

	send_message(victim, NULL, message);

	od_printf(" %u troops, %u generals and %u fighters return home.\r\n", troops, generals, fighters);
	od_printf(" %u enemy troops, %u enemy generals and %u enemy defence stations were destroyed\r\n", enemy_troops, enemy_generals, enemy_defence_stations);

	od_printf("\r\nPress a key to continue\r\n");
	od_get_key(TRUE);

	attacker->troops += troops;
	attacker->generals += generals;
	attacker->fighters += fighters;

	victim->troops -= enemy_troops;
	victim->generals -= enemy_generals;
	victim->defence_stations -= enemy_defence_stations;

}


void perform_maintenance()
{
	ibbsmsg_t msg;
	ibbsmsg_t outboundmsg;
	int i;
	int id;
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
	time_t lastrun;
	time_t timenow;
	FILE *fptr, *fptr2;
	int newnodenum;
	char message2[256];
	int stage = 0;
	timenow = time(NULL);
	int reset = 0;

	dolog("Performing maintenance...");

	// parse all incoming messages
	i = 0;
    k = 0;
	if (interBBSMode == 1) {
		while (1) {
		    result = IBGet(&InterBBSInfo, &msg, sizeof(ibbsmsg_t));
			
	 	    if (result == eSuccess) {
				msg2he(&msg);
				if (game_id == 0) {
					game_id = msg.game_id;
					fptr = fopen("game_id.dat", "wb");
					if (fptr) {
						fwrite(&game_id, sizeof(uint32_t), 1, fptr);
						fclose(fptr);
					}
				}
				if ((msg.turns_in_protection != turns_in_protection || msg.turns_per_day != turns_per_day) && msg.from != 1) {
					fprintf(stderr, "Settings mismatch. Ignoring packet\n");
					continue;
				} else if ((msg.turns_in_protection != turns_in_protection || msg.turns_per_day != turns_per_day) && msg.from == 1) {
					fptr = fopen("galactic.ini", "w");
					if (!fptr) {
						fprintf(stderr, "Unable to open galactic.ini\n");
					} else {
						fprintf(fptr, "[Main]\n");
						fprintf(fptr, "Turns in Protection = %d\n", msg.turns_in_protection);
						fprintf(fptr, "Turns per Day = %d\n", msg.turns_per_day);
						if (log_path != NULL) {
							fprintf(fptr, "Log Path = %s\n", log_path);
						} else {
							fprintf(fptr, ";Log Path = logs\n");
						}
						fprintf(fptr, "\n");
						fprintf(fptr, "[InterBBS]\n");
						fprintf(fptr, "Enabled = True\n");
						fprintf(fptr, "System Name = %s\n", InterBBSInfo.myNode->name);
						fprintf(fptr, "League Number = %d\n", InterBBSInfo.league);
						fprintf(fptr, "File Inbox = %s\n", InterBBSInfo.myNode->filebox);
						fprintf(fptr, "Default Outbox = %s\n", InterBBSInfo.defaultFilebox);
						fclose(fptr);

						turns_in_protection = msg.turns_in_protection;
						turns_per_day = msg.turns_per_day;
					}
				}
			switch(msg.type) {
			case 1:
				// add score to database
				if (game_id != msg.game_id) {
					dolog("Got packet for incorrect game id, skipping");
					break;
				}
				dolog("Got score file packet for player %s", msg.player_name);
				rc = sqlite3_open("interbbs.db3", &db);
				if (rc) {
					// Error opening the database
					fprintf(stderr, "Error opening interbbs database: %s\n", sqlite3_errmsg(db));
					sqlite3_close(db);
					return;
				}
				sqlite3_busy_timeout(db, 5000);
				snprintf(sqlbuffer, 256, "SELECT id, last FROM scores WHERE gamename=? and address=?");
				sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);
				sqlite3_bind_text(stmt, 1, msg.player_name, strlen(msg.player_name) + 1, SQLITE_STATIC);
				sqlite3_bind_int(stmt, 2, msg.from);

				rc = sqlite3_step(stmt);

				if (rc == SQLITE_ROW) {
					id = sqlite3_column_int(stmt, 0);
					last_score = sqlite3_column_int(stmt, 1);
					sqlite3_finalize(stmt);
					if (last_score < msg.created) {
						snprintf(sqlbuffer, 256, "UPDATE scores SET score=?, last=? WHERE id=?");
						sqlite3_prepare_v2(db, sqlbuffer, strlen(sqlbuffer) + 1, &stmt, NULL);
						sqlite3_bind_int(stmt, 1, msg.score);
						sqlite3_bind_int(stmt, 2, msg.created);
						sqlite3_bind_int(stmt, 3, id);
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
				if (game_id != msg.game_id) {
					dolog("Got packet for incorrect game id, skipping");
					break;
				}			
				dolog("Got invasion packet for: %s from: %s", msg.victim_name, msg.player_name);
				// perform invasion
				if (do_interbbs_battle(msg.victim_name, msg.player_name, msg.from, msg.troops, msg.generals, msg.fighters, &outboundmsg) == 0) {
					outboundmsg.turns_in_protection = turns_in_protection;
					outboundmsg.turns_per_day = turns_per_day;
					outboundmsg.game_id = game_id;	
					msg2ne(&outboundmsg);
					IBSend(&InterBBSInfo, msg.from, &outboundmsg, sizeof(ibbsmsg_t));
				} else {
					dolog("Invasion failed");
				}
				break;
			case 3:
				if (game_id != msg.game_id) {
					dolog("Got packet for incorrect game id, skipping");
					break;
				}			
				// return troops
				dolog("Got return troops packet for: %s", msg.player_name);
				player = load_player_gn(msg.player_name);
				if (player != NULL) {
					player->troops += msg.troops;
					player->generals += msg.generals;
					player->fighters += msg.fighters;

					if (msg.score == 1) {
						player->population += msg.plunder_people;
						player->credits += msg.plunder_credits;
						player->food += msg.plunder_food;
						snprintf(message, 256, "Your armarda returned victorious, %u troops, %u generals and %u fighters returned with %u prisoners, %u credits and %u food.",
							msg.troops, msg.generals, msg.fighters, msg.plunder_people, msg.plunder_credits, msg.plunder_food);

					} else if (msg.score == 0) {
						snprintf(message, 256, "Your armarda returned defeated, %u troops, %u generals and %u fighters returned.",
							msg.troops, msg.generals, msg.fighters);
					} else {
						snprintf(message, 256, "Your armarda encounted galactic protection and all your troops returned disappointed.");
					}
					send_message(player, NULL, message);
					save_player(player);
					free(player);
				} else {
					dolog("return troops failed");
				}
				break;
			case 4:
				// message
				if (game_id != msg.game_id) {
					dolog("Got packet for incorrect game id, skipping");
					break;
				}				
				rc = sqlite3_open("interbbs.db3", &db);
				if (rc) {
					// Error opening the database
					printf("Error opening interbbs database: %s\n", sqlite3_errmsg(db));
					sqlite3_close(db);
					return;
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
			case 5:
				// new node
				if (game_id != msg.game_id) {
					dolog("Got packet for incorrect game id, skipping");
					break;
				}				
				if (msg.from != 1) {
					fprintf(stderr, "Received ADD/REMOVE from system not Node 1\n");
					break;
				}
				if (strcmp(msg.victim_name, "ADD") == 0) {
					newnodenum = atoi(msg.player_name);
					fptr = fopen("BBS.CFG", "a");
					if (!fptr) {
						fprintf(stderr, "Unable to open BBS.CFG\n");
						break;
					}
					fprintf(fptr, "\r\nLinkNodeNumber %d\r\n", newnodenum);
					fprintf(fptr, "LinkName %s\r\n", msg.message);
					fclose(fptr);
				} else if (strcmp(msg.victim_name, "REMOVE")) {
					newnodenum = atoi(msg.player_name);
					fptr = fopen("BBS.CFG", "r");
					if (!fptr) {
						fprintf(stderr, "Unable to open BBS.CFG\n");
						break;
					}

					fptr2 = fopen("BBS.CFG.BAK", "w");
					if (!fptr2) {
						fprintf(stderr, "Unable to open BBS.CFG.BAK\n");
						break;
					}
					fgets(message, 256, fptr);
					while (!feof(fptr)) {
						fputs(message, fptr2);
						fgets(message, 256, fptr);
					}
					fclose(fptr2);
					fclose(fptr);
					fptr = fopen("BBS.CFG.BAK", "r");
					if (!fptr) {
						fprintf(stderr, "Unable to open BBS.CFG.BAK\n");
						break;
					}

					fptr2 = fopen("BBS.CFG", "w");
					if (!fptr2) {
						fprintf(stderr, "Unable to open BBS.CFG\n");
						break;
					}

					sprintf(message2, "LinkNodeNumber %d", newnodenum);

					fgets(message, 256, fptr);
					while (!feof(fptr)) {
						if (strncasecmp(message, message2, strlen(message2)) == 0) {
							stage = 1;
						} else if (strncasecmp(message, "LinkNodeNumber", 14) == 0 && stage ==1) {
							stage = 0;
						}
						if (stage == 0) {
							fputs(message, fptr2);
						}
						fgets(message, 256, fptr);
					}
					fclose(fptr2);
					fclose(fptr);
					unlink("BBS.CFG.BAK");
				}
				break;
			case 6:
				if (msg.from == 1) {
					reset = 1;
					game_id = msg.game_id;
				} else {
					fprintf(stderr, "Got reset message from someone not node 1, ignoring\n");
				}
				break;			
			default:
				fprintf(stderr, "Unknown message type: %d\n", msg.type);
				break;
			}
			i++;
		    } else if (result == eForwarded) {
			k++;
		    } else {
			break;
		    }
		}
		if (reset == 1) {
			fprintf(stderr, "Got reset message! resetting the game...\n");
#ifdef _MSC_VER
			system("reset.bat");
#else
			system("./reset.sh");
#endif				
			if(unlink("inuse.flg") != 0) {
				perror("unlink ");
			}
			
			fptr = fopen("game_id.dat", "wb");
			if (!fptr) {
				dolog("Could not open game_id.dat for writing!!\n");
			} else {
				fwrite(&game_id, sizeof(uint32_t), 1, fptr);
				fclose(fptr);
			}
			exit(0);
		}
		fprintf(stderr, "Parsed %d inbound messages\nForwarded %d messages\n", i, k);


		// send all score messages
		rc = sqlite3_open("users.db3", &db);
		if (rc) {
			// Error opening the database
			fprintf(stderr, "Error opening user database: %s\n", sqlite3_errmsg(db));
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
					msg.turns_in_protection = turns_in_protection;
					msg.turns_per_day = turns_per_day;
					msg.game_id = game_id;				
					msg2ne(&msg);
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
	od_printf("`white` - Score        : %u\r\n", calculate_score(player));
	od_printf(" - Population   : %u million\r\n", player->population);
	od_printf(" - Food         : %u tonnes\r\n", player->food);
	od_printf(" - Credits      : %u\r\n", player->credits);
	od_printf(" - Troops       : %u\r\n", player->troops);
	od_printf(" - Generals     : %u\r\n", player->generals);
	od_printf(" - Fighters     : %u\r\n", player->fighters);
	od_printf(" - Spies        : %u\r\n", player->spies);
	od_printf(" - Def. Stations: %u\r\n", player->defence_stations);
	od_printf(" - Command Ship : %d%% complete\r\n", player->command_ship);
	od_printf(" - Planets      : %u\r\n", player->planets_food + player->planets_ore + player->planets_military + player->planets_industrial + player->planets_urban);
	od_printf("   (Ore %u) (Food %u) (Soldier %u) (Industrial %u) (Urban %u)\r\n", player->planets_ore, player->planets_food, player->planets_military, player->planets_industrial, player->planets_urban);
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
				if (type == 1) {
					od_printf("\r\nYou can't send a message to yourself\r\n'");
				} else if (type == 2) {
					od_printf("\r\nYou can't attack yourself!\r\n");
				} else if (type == 3) {
					od_printf("\r\nYou can't spy on yourself!\r\n");
				}
			} else if (victim->total_turns < turns_in_protection && type > 1) {
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
	uint32_t troop_wages;
	uint32_t citizen_hunger;
	uint32_t total_industrial;
	uint32_t total_ore;
	int done;
	int bank_done;
	float starvation;
	float loyalty;
	
	char message[256];
	char buffer[10];
	ibbsmsg_t msg;
	int addr;

	player_t *victim;

	int i;
	int j;
	char c;

	uint32_t troops_to_send;
	uint32_t generals_to_send;
	uint32_t fighters_to_send;
	int event_rand;
	int event_mod;

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
			od_printf("  (`bright green`D`white`) Done\r\n");
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
								msg.turns_in_protection = turns_in_protection;
								msg.turns_per_day = turns_per_day;
								msg.game_id = game_id;	
								msg2ne(&msg);
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
		od_printf("Your military requires `bright yellow`%u`white` credits in wages.\r\n", troop_wages);
		od_printf("Pay them (`bright green`%u`white`) : ", (troop_wages < player->credits ? troop_wages : player->credits));

		od_input_str(buffer, 8, '0', '9');

		if (strlen(buffer) == 0) {
			i = (troop_wages < player->credits ? troop_wages : player->credits);
		} else {
			i = atoi(buffer);
			if (i > player->credits) {
				i = player->credits;
			}
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
		od_printf("Your citizens need `bright yellow`%u`white` tonnes of food.\r\n", citizen_hunger);
		od_printf("Feed them (`bright green`%u`white`) : ", (citizen_hunger < player->food ? citizen_hunger : player->food));

		od_input_str(buffer, 8, '0', '9');

		if (strlen(buffer) == 0) {
			i = (citizen_hunger < player->food ? citizen_hunger : player->food);
		} else {
			i = atoi(buffer);
			if (i > player->food) {
				i = player->food;
			}
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
			od_printf("`white` Buy Stuff                 Your funds: %u credits\r\n", player->credits);
			od_printf("`bright green`============================`green`[`white`Price`green`]`bright green`=`green`[`white`You Have`green`]`bright green`=`green`[`white`Can Afford`green`]`bright green`=\r\n");
			od_printf("`white` (1) Troops                    100    %6u     %6u\r\n", player->troops, player->credits / 100);
			od_printf(" (2) Generals                  500    %6u     %6u\r\n", player->generals, player->credits / 500);
			od_printf(" (3) Fighters                 1000    %6u     %6u\r\n", player->fighters, player->credits / 1000);
			od_printf(" (4) Defence Stations         1000    %6u     %6u\r\n", player->defence_stations, player->credits / 1000);
			if (player->command_ship == 100) {
				od_printf(" (5) Command Ship              N/A    %6u%%  `bright green`complete`white`\r\n", player->command_ship);
			} else {
				od_printf(" (5) Command Ship %16u    %6u%%       %4s\r\n", 10000 * (player->command_ship + 1), player->command_ship, (player->credits >= 10000 * (player->command_ship + 1) ? "`bright green`yes`white`" : "`bright red` no`white`" ));
			}
			od_printf(" (6) Colonize Planets         2000    %6u     %6u\r\n", player->planets_ore + player->planets_food + player->planets_industrial + player->planets_military + player->planets_urban, player->credits / 2000);
			od_printf(" (7) Food                      100    %6u     %6u\r\n", player->food, player->credits / 100);
			od_printf(" (8) Spies                    5000    %6u     %6u\r\n", player->spies, player->credits / 5000);
			od_printf("\r\n (9) Visit the Bank\r\n");
			od_printf(" (0) Disband Armies\r\n");
			od_printf("\r\n");
			od_printf(" (`bright green`D`white`) Done\r\n");
			od_printf("`bright green`============================================================`white`\r\n");

			c = od_get_answer("1234567890dD\r");
			switch (c) {
				case '1':
					od_printf("How many troops do you want to buy? (MAX `bright yellow`%u`white`) ", player->credits / 100);
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
					od_printf("How many generals do you want to buy? (MAX `bright yellow`%u`white`) ", player->credits / 500);
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
					od_printf("How many fighters do you want to buy? (MAX `bright yellow`%u`white`) ", player->credits / 1000);
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
					od_printf("How many defence stations do you want to buy? (MAX `bright yellow`%u`white`) ", player->credits / 1000);
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
					od_printf("Add to your command ship? (Y/`bright green`N`white`) ");
					c = od_get_answer("YyNn\r");
					
					if (c == 'y' || c == 'Y') {
						if (player->command_ship >= 100) {
							od_printf("\r\n`bright red`You can't buy any more!`white`\r\n");
						} else if ((player->command_ship + 1) * 10000 > player->credits) {
							od_printf("\r\n`bright red`You can't afford that!`white`\r\n");
						} else {
							player->command_ship++;
							player->credits -= player->command_ship * 10000;
						}
					}
					break;
				case '6':
					od_printf("How many planets do you want to buy? (MAX `bright yellow`%u`white`) ", player->credits / 2000);
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
							od_printf("  5. Urban\r\n");
							c = od_get_answer("12345");
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
							case '5':
								player->planets_urban += i;
								player->credits -= i * 2000;							
							}
						}
					}
					break;
				case '7':
					od_printf("How much food do you want to buy? (MAX `bright yellow`%u`white`) ", player->credits / 100);
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
					od_printf("How many spies do you want to buy? (MAX `bright yellow`%u`white`) ", player->credits / 5000);
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
				case '9':
					{
						bank_done = 0;
						while (!bank_done) {
							if (player->bank_balance >= 0) {
#ifdef _MSC_VER
								od_printf("Your current bank balance is `bright green`%I64d `white`credits.\r\n", player->bank_balance);
								od_printf("Interest is 0.1%% (%d credits) per day .\r\n", (int)((float)player->bank_balance * 0.001f));
#else								
								od_printf("Your current bank balance is `bright green`%lld `white`credits.\r\nInterest is 0.1%% (%d credits) per day.\r\n", player->bank_balance, (int)((float)player->bank_balance * 0.001f));
#endif
							} else {
#ifdef _MSC_VER
								od_printf("Your current bank balance is `bright red`%I64d `white`credits.\r\n", player->bank_balance);
								od_printf("Interest is 5%% (%d credits) per day.\r\n", abs((int)((float)player->bank_balance * .05f)));
#else
								od_printf("Your current bank balance is `bright red`%lld `white`credits.\r\nInterest is 5%% (%d credits) per day.\r\n", player->bank_balance, abs((int)((float)player->bank_balance * 0.05f)));
#endif
							}
							od_printf("Your current allowed overdraft is %u credits.\r\n", (calculate_score(player) * 100) / 2);
							od_printf("Would you like to (D)eposit, (W)ithdraw or (`bright green`L`white`)eave? ");
							c = od_get_answer("DdWwLl\r");
							switch (c) {
								case 'D':
								case 'd':
									od_printf("\r\n\r\nHow much would you like to deposit (0 - %u credits) ? ", player->credits);
									od_input_str(buffer, 10, '0', '9');
									if (strlen(buffer) != 0) {
										i = atoi(buffer);
										if (i > player->credits) {
											od_printf("\r\n`bright red`You don't have that many credits!`white`\r\n");
										} else {
											player->credits -= i;
											player->bank_balance += i;
										}
									}								
									break;
								case 'W':
								case 'w':
									od_printf("\r\n\r\nHow much would you like to withdraw (0 - %u credits) ? ", player->bank_balance + ((calculate_score(player) * 100) / 2));
									od_input_str(buffer, 10, '0', '9');
									if (strlen(buffer) != 0) {
										i = atoi(buffer);
										if (i > player->bank_balance + ((calculate_score(player) * 100) / 2)) {
											od_printf("\r\n`bright red`You don't have that many credits!`white`\r\n");
										} else {
											player->bank_balance -= i;
											player->credits += i;
										}
									}									
									break;
								default:
									od_printf("\r\n\r\n");
									bank_done = 1;
									break;
							}
						}
					}
					break;
				case '0':
					od_printf("\r\n\r\n`bright red`Warning: `bright white`You gain no compensation for disbanding armies.`white`\r\n\r\n");
					if (player->troops > 0) {
						od_printf("Disband how many troops? (`bright green`0`white` - %d) ", player->troops);
						od_input_str(buffer, 10, '0', '9');
						if (strlen(buffer) != 0) {
							i = atoi(buffer);
							if (i > player->troops) {
								i = player->troops;
							}
							player->troops -= i;
						}
					}
					if (player->generals > 0) {
						od_printf("Disband how many generals? (`bright green`0`white` - %d) ", player->generals);
						od_input_str(buffer, 10, '0', '9');
						if (strlen(buffer) != 0) {
							i = atoi(buffer);
							if (i > player->generals) {
								i = player->generals;
							}
							player->generals -= i;
						}
					}	
					if (player->fighters > 0) {
						od_printf("Disband how many fighters? (`bright green`0`white` - %d) ", player->fighters);
						od_input_str(buffer, 10, '0', '9');
						if (strlen(buffer) != 0) {
							i = atoi(buffer);
							if (i > player->fighters) {
								i = player->fighters;
							}
							player->fighters -= i;
						}
					}
					if (player->defence_stations > 0) {
						od_printf("Disband how many defence stations? (`bright green`0`white` - %d) ", player->defence_stations);
						od_input_str(buffer, 10, '0', '9');
						if (strlen(buffer) != 0) {
							i = atoi(buffer);
							if (i > player->defence_stations) {
								i = player->defence_stations;
							}
							player->defence_stations -= i;
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
			od_printf("`white` Covert Operations                 Spies: %u\r\n", player->spies);
			od_printf("`bright magenta`=============================`magenta`[`white`Price`magenta`]`bright magenta`========================\r\n");
			od_printf("`white` (1) Spy on someone............1000\r\n");
			od_printf(" (`bright green`D`white`) Done\r\n");
			od_printf("`bright magenta`============================================================`white`\r\n");
			c = od_get_answer("1dD\r");
			switch(tolower(c)) {
			case '1':
				victim = select_victim(player, "Who do you want to spy on", 3);
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
		od_printf("Do you want to launch an attack? (Y/`bright green`N`white`) ");
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
							od_printf("\r\nSend how many troops? (MAX %u) ", player->troops);
							od_input_str(buffer, 8, '0', '9');
							if (strlen(buffer) > 0) {
								i = atoi(buffer);
								if (i > player->troops) {
									od_printf("\r\nYou don't have that many!\r\n");
								} else if (i > 0) {
									od_printf("\r\nSending %u troops.\r\n", i);
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
								od_printf("\r\nSend how many generals? (MAX %u) ", player->generals);
								od_input_str(buffer, 8, '0', '9');
								if (strlen(buffer) > 0) {
									i = atoi(buffer);
									if (i > player->generals) {
										od_printf("\r\nYou don't have that many!\r\n");
									} else {
										od_printf("\r\nSending %u generals.\r\n", i);
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
								od_printf("\r\nSend how many fighters? (MAX %u) ", player->fighters);
								od_input_str(buffer, 8, '0', '9');
								if (strlen(buffer) > 0) {
									i = atoi(buffer);
									if (i > player->fighters) {
										od_printf("\r\nYou don't have that many!\r\n");
									} else {
										od_printf("\r\nSending %u fighters.\r\n", i);
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
			od_printf("\r\nDo you want to launch an Inter-Galactic Armarda? (Y/`bright green`N`white`) ");
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
									od_printf("\r\nSend how many troops? (MAX %u) ", player->troops);
									od_input_str(buffer, 8, '0', '9');
									if (strlen(buffer) > 0) {
										i = atoi(buffer);
										if (i > player->troops) {
											od_printf("\r\nYou don't have that many!\r\n");
										} else if (i > 0) {
											od_printf("\r\nSending %u troops.\r\n", i);
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
										od_printf("\r\nSend how many generals? (MAX %u) ", player->generals);
										od_input_str(buffer, 8, '0', '9');
										if (strlen(buffer) > 0) {
											i = atoi(buffer);
											if (i > player->generals) {
												od_printf("\r\nYou don't have that many!\r\n");
											} else {
												od_printf("\r\nSending %u generals.\r\n", i);
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
										od_printf("\r\nSend how many fighters? (MAX %u) ", player->fighters);
										od_input_str(buffer, 8, '0', '9');
										if (strlen(buffer) > 0) {
											i = atoi(buffer);
											if (i > player->fighters) {
												od_printf("\r\nYou don't have that many!\r\n");
											} else {
												od_printf("\r\nSending %u fighters.\r\n", i);
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
								msg.turns_in_protection = turns_in_protection;
								msg.turns_per_day = turns_per_day;
								msg.game_id = game_id;									
								msg2ne(&msg);
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
			od_printf("Your food planets produced %u food.\r\n", 10 * player->planets_food);

			player->food += 10 * player->planets_food;
		}

		if (player->planets_military > 0) {
			od_printf("Your soldier planets provided %u troops\r\n", player->planets_military * 10);

			player->troops += 10 * player->planets_military;
		}

		if (player->planets_ore > 0) {
			if (player->planets_ore > (uint32_t)(player->population / 25.f)) {
				total_ore = (uint32_t)(player->population / 25.f * 1000.f);
			} else {
				total_ore = player->planets_ore * 1000;
			}

			od_printf("Your ore planets mined %u worth of minerals\r\n", total_ore);

			player->credits += total_ore;
		}

		if (player->planets_industrial > 0) {

			if (player->planets_industrial > (uint32_t)(player->population / 25.f)) {
				total_industrial = (uint32_t)(player->population / 25.f * 1000.f);
			} else {
				total_industrial = player->planets_industrial * 1000;
			}

			od_printf("Your industrial planets produced %u worth of goods\r\n", total_industrial);

			player->credits += total_industrial;
		}
		od_printf("Taxes produce %u credits\r\n", player->population * 23);

		player->credits += player->population * 23;




		// Population Changes
		if (starvation < 1) {
			od_printf("%u citizens died from starvation\r\n", (uint32_t)(player->population - ((float)player->population * starvation)));
			player->population -= player->population - (player->population * starvation);
		} else {
			if (player->population > player->planets_urban * 50) {
				od_printf("No population increase due to not enough urban planets\r\n");
			} else {
				od_printf("%u new citizens join the empire\r\n", (uint32_t)((float)player->population * 0.05));
				player->population += player->population * 0.05;
			}
		}

		if (loyalty < 1) {
			od_printf("%u troops fled the empire\r\n", (uint32_t)(player->troops - ((float)player->troops * loyalty)));
			player->troops -= player->troops - (player->troops * loyalty);
		}

		do_lua_script("events");

		// loop
		player->turns_left--;
		player->total_turns++;
		save_player(player);

		if (player->turns_left > 0) {
			od_printf("\r\n`bright yellow`Continue`white` ? (`bright green`Y`white`/N) ");
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

void door_quit(void) {
	if (full == 1) {
		perform_maintenance();
	}
	if(unlink("inuse.flg") != 0) {
		perror("unlink ");
	}
}

void lua_push_cfunctions(lua_State *L) {
    lua_pushcfunction(L, lua_getTroops);
    lua_setglobal(L, "gd_get_troops");
    lua_pushcfunction(L, lua_getGenerals);
    lua_setglobal(L, "gd_get_generals");
    lua_pushcfunction(L, lua_getFighters);
    lua_setglobal(L, "gd_get_fighters");
    lua_pushcfunction(L, lua_getDefenceStations);
    lua_setglobal(L, "gd_get_defence_stations");
    lua_pushcfunction(L, lua_getSpies);
    lua_setglobal(L, "gd_get_spies");
    lua_pushcfunction(L, lua_getPopulation);
    lua_setglobal(L, "gd_get_population");
    lua_pushcfunction(L, lua_getFood);
    lua_setglobal(L, "gd_get_food");
    lua_pushcfunction(L, lua_getCredits);
    lua_setglobal(L, "gd_get_credits");
    lua_pushcfunction(L, lua_setTroops);
    lua_setglobal(L, "gd_set_troops");
    lua_pushcfunction(L, lua_setGenerals);
    lua_setglobal(L, "gd_set_generals");
    lua_pushcfunction(L, lua_setFighters);
    lua_setglobal(L, "gd_set_fighters");
    lua_pushcfunction(L, lua_setDefenceStations);
    lua_setglobal(L, "gd_set_defence_stations");
    lua_pushcfunction(L, lua_setSpies);
    lua_setglobal(L, "gd_set_spies");
    lua_pushcfunction(L, lua_setPopulation);
    lua_setglobal(L, "gd_set_population");
    lua_pushcfunction(L, lua_setFood);
    lua_setglobal(L, "gd_set_food");
    lua_pushcfunction(L, lua_setCredits);
    lua_setglobal(L, "gd_set_credits");
    lua_pushcfunction(L, lua_printYellow);
    lua_setglobal(L, "gd_print_yellow");
	lua_pushcfunction(L, lua_printGreen);
    lua_setglobal(L, "gd_print_green");	
}

#if _MSC_VER
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,LPSTR lpszCmdLine,int nCmdShow)
{
#else
int main(int argc, char **argv)
{
#endif
	char bbsname[256];
	time_t timenow;
	struct tm today_tm;
	struct tm last_tm;
	struct tm *ptr;
	char c;
	int i;
	struct stat s;
	int inuse = 0;
	FILE *fptr;
	int j;
	int newnodenum;
	int start;
	int end;
	char newnodename[256];
	ibbsmsg_t msg;
	int days_passed;
	full = 0;

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

	log_path = NULL;

	if (ini_parse("galactic.ini", handler, NULL) <0) {
		fprintf(stderr, "Unable to load galactic.ini\n");
	}

	if (interBBSMode == 1) {
		if (IBReadConfig(&InterBBSInfo, "BBS.CFG")!= eSuccess) {
			interBBSMode = 0;
		}
		fptr = fopen("game_id.dat", "rb");
		if (!fptr) {
			game_id = 0;
		} else {
			fread(&game_id, sizeof(uint32_t), 1, fptr);
			fclose(fptr);
		}
	}

	if (stat("inuse.flg", &s) == 0) {
		inuse = 1;
	}

#if _MSC_VER
	if (strcasecmp(lpszCmdLine, "maintenance") == 0) {
		if (inuse == 1) {
			fprintf(stderr, "Game currently inuse.\n");
			return 2;
		} else {
			fptr = fopen("inuse.flg", "w");
			fputs("INUSE!", fptr);
			fclose(fptr);
			perform_maintenance();
			if(unlink("inuse.flg") != 0) {
				perror("unlink ");
			}
			return 0;
		}		
	}
	if (strcasecmp(lpszCmdLine, "reset") == 0) {
		game_id = rand() % 0xFFFFFFFE + 1;
		fptr = fopen("game_id.dat", "wb");
		if (!fptr) {
			fprintf(stderr, "Failed to open game_id.dat\n");
			return -1;
		}
		
		fwrite(&game_id, sizeof(uint32_t), 1, fptr);
		fclose(fptr);
				
		memset(&msg, 0, sizeof(ibbsmsg_t));
		msg.type = 6;
		msg.from = InterBBSInfo.myNode->nodeNumber;
		msg.created = time(NULL);
		msg.turns_per_day = turns_per_day;
		msg.turns_in_protection = turns_in_protection;
		msg.game_id = game_id;
		msg2ne(&msg);
		IBSendAll(&InterBBSInfo, &msg, sizeof(ibbsmsg_t));	
		system("reset.bat");
		return 0;		
	}

	start = 0;
	if (strncasecmp(lpszCmdLine, "-ADD", 4) == 0 || strncasecmp(lpszCmdLine, "/ADD", 4) == 0) {
		for (i=5;i<strlen(lpszCmdLine);i++) {
			if (lpszCmdLine[i] == '\"' && start == 0) {
				start = i+1;
			} else if (lpszCmdLine[i] == '\"') {
				end = i;
				break;
			}
		}
		
		if (end - start < 255) {
			strncpy(newnodename, &lpszCmdLine[start], end - start);
			newnodenum = atoi(&lpszCmdLine[5]);

			memset(&msg, 0, sizeof(ibbsmsg_t));

			msg.type = 5;
			msg.from = InterBBSInfo.myNode->nodeNumber;
			sprintf(msg.player_name, "%d", newnodenum);
			strcpy(msg.victim_name, "ADD");
			sprintf(msg.message, "%s", newnodename);
			msg.created = time(NULL);
			msg.turns_per_day = turns_per_day;
			msg.turns_in_protection = turns_in_protection;
			msg.game_id = game_id;	
			msg2ne(&msg);
			IBSendAll(&InterBBSInfo, &msg, sizeof(ibbsmsg_t));
		}
		return 0;
	}

	if (strncasecmp(lpszCmdLine, "-DEL", 4) == 0 || strncasecmp(lpszCmdLine, "/DEL", 4) == 0) {
		if (end - start < 255) {
			newnodenum = atoi(&lpszCmdLine[5]);

			memset(&msg, 0, sizeof(ibbsmsg_t));

			msg.type = 5;
			msg.from = InterBBSInfo.myNode->nodeNumber;
			sprintf(msg.player_name, "%d", newnodenum);
			strcpy(msg.victim_name, "REMOVE");
			msg.created = time(NULL);
			msg.turns_per_day = turns_per_day;
			msg.turns_in_protection = turns_in_protection;
			msg.game_id = game_id;	
			msg2ne(&msg);
			IBSendAll(&InterBBSInfo, &msg, sizeof(ibbsmsg_t));
		}
		return 0;
	}	

	for (i=0;i<strlen(lpszCmdLine);i++) {
       	if (strncasecmp(&lpszCmdLine[i], "-FULL", 5) == 0 || strncasecmp(&lpszCmdLine[i], "/FULL", 5) == 0) {
            full = 1;
        }
	}	
	od_parse_cmd_line(lpszCmdLine);
#else
	if (argc > 1 && strcasecmp(argv[1], "maintenance") == 0) {
		if (inuse == 1) {
			fprintf(stderr, "Game currently in use.\n");
			return 2;
		} else {
			fptr = fopen("inuse.flg", "w");
			fputs("INUSE!", fptr);
			fclose(fptr);		
			perform_maintenance();
			if(unlink("inuse.flg") != 0) {
				perror("unlink ");
			}
			return 0;
		}
	
	}

	if (argc > 1 && strcasecmp(argv[1], "reset") == 0) {
		game_id = rand() % 0xFFFFFFFE + 1;
		fptr = fopen("game_id.dat", "wb");
		if (!fptr) {
			fprintf(stderr, "Failed to open game_id.dat\n");
			return -1;
		}
		
		fwrite(&game_id, sizeof(uint32_t), 1, fptr);
		fclose(fptr);
						
		memset(&msg, 0, sizeof(ibbsmsg_t));
		msg.type = 6;
		msg.from = InterBBSInfo.myNode->nodeNumber;
		msg.created = time(NULL);
		msg.turns_per_day = turns_per_day;
		msg.turns_in_protection = turns_in_protection;
		msg.game_id = game_id;
		msg2ne(&msg);
		IBSendAll(&InterBBSInfo, &msg, sizeof(ibbsmsg_t));
		system("./reset.sh");	
		return 0;	
	}

	if (argc > 1 && (strcasecmp(argv[1], "-ADD") == 0 || strcasecmp(argv[1], "/ADD") == 0)) {
			memset(&msg, 0, sizeof(ibbsmsg_t));

			msg.type = 5;
			msg.from = InterBBSInfo.myNode->nodeNumber;
			sprintf(msg.player_name, "%s", argv[2]);
			strcpy(msg.victim_name, "ADD");
			sprintf(msg.message, "%s", argv[3]);
			msg.created = time(NULL);
			msg.turns_per_day = turns_per_day;
			msg.turns_in_protection = turns_in_protection;
			msg.game_id = game_id;	
			msg2ne(&msg);

			printf("sending to all\n");
			printf("%d\n", IBSendAll(&InterBBSInfo, &msg, sizeof(ibbsmsg_t)));
			return 0;
	}

	if (argc > 1 && (strcasecmp(argv[1], "-DEL") == 0 || strcasecmp(argv[1], "/DEL") == 0)) {
			memset(&msg, 0, sizeof(ibbsmsg_t));

			msg.type = 5;
			msg.from = InterBBSInfo.myNode->nodeNumber;
			sprintf(msg.player_name, "%s", argv[2]);
			strcpy(msg.victim_name, "REMOVE");
			msg.created = time(NULL);
			msg.turns_per_day = turns_per_day;
			msg.turns_in_protection = turns_in_protection;
			msg.game_id = game_id;	
			msg2ne(&msg);
			IBSendAll(&InterBBSInfo, &msg, sizeof(ibbsmsg_t));
			return 0;
	}

	for (i=1;i<argc;i++) {
		if (strcasecmp(argv[i], "/full") == 0 || strcasecmp(argv[i], "-full") == 0) {
			full = 1;
		}
	}

	od_parse_cmd_line(argc, argv);
#endif
	
	od_init();

	if (inuse == 1) {
		od_printf("Sorry, the game is currently in use. Please try again later.\r\n");
		od_get_key(TRUE);
		od_exit(2, FALSE);
	}

	atexit(door_quit);
	fptr = fopen("inuse.flg", "w");
	fputs("INUSE!", fptr);
	fclose(fptr);	

	od_send_file("logo.ans");
	od_get_key(TRUE);

	snprintf(bbsname, 255, "%s!%s", od_control_get()->user_name, od_control_get()->user_handle);

	gPlayer = load_player(bbsname);
	if (gPlayer == NULL) {
		gPlayer = new_player(bbsname);
		if (gPlayer == NULL) {
			od_exit(0, FALSE);
			exit(0);
		} else {
			save_player(gPlayer);
			gPlayer = load_player(bbsname);
		}
	}

	ptr = localtime(&gPlayer->last_played);
	memcpy(&last_tm, ptr, sizeof(struct tm));
	timenow = time(NULL);
	ptr = localtime(&timenow);
	memcpy(&today_tm, ptr, sizeof(struct tm));

	days_passed =  0;

	if (today_tm.tm_yday != last_tm.tm_yday || today_tm.tm_year != last_tm.tm_year) {
		if (today_tm.tm_year != last_tm.tm_year) {
			days_passed = 365 - last_tm.tm_yday + today_tm.tm_yday;
		} else {
			days_passed = today_tm.tm_yday - last_tm.tm_yday;
		}
		

		
		gPlayer->turns_left = turns_per_day;
	}
	gPlayer->last_played = timenow;

	do {
		od_printf("\r\n`white`Game Menu\r\n");
		od_printf("`red`--------------------------------------\r\n");
		od_printf(" `white`(`bright yellow`1`white`) Play Game\r\n");
		od_printf(" `white`(`bright yellow`2`white`) See Messages\r\n");
		od_printf(" `white`(`bright yellow`3`white`) See Status\r\n");
		od_printf(" `white`(`bright yellow`4`white`) See Scores\r\n");
		if (interBBSMode == 1) {
			od_printf(" `white`(`bright yellow`5`white`) See InterBBS Nodes\r\n");
			od_printf(" `white`(`bright yellow`6`white`) See InterBBS Scores\r\n");
		}
		od_printf(" `white`(`bright yellow`Q`white`) Exit Game\r\n");
		od_printf("`red`--------------------------------------`white`\r\n");
		od_printf("Your Choice? ");
		if (interBBSMode == 1) {
			c = od_get_answer("123456qQ");
		} else {
			c = od_get_answer("1234qQ");
		}
		od_printf("\r\n");
		switch (c) {
		case '1':
			if (days_passed > 0) {
				od_printf("\r\n\r\nIt's been %d days since you last played!\r\n", days_passed);
				if (gPlayer->bank_balance > 0) {
					od_printf("You've earned %d credits in interest on your bank balance!\r\n", (int)((float)gPlayer->bank_balance * 0.001f * (float)days_passed));
					gPlayer->bank_balance +=  (int)((float)gPlayer->bank_balance * 0.001f * (float)days_passed);
				} if (gPlayer->bank_balance < 0) {
					od_printf("You've been charged %d credits in interest on your bank balance!\r\n", abs((int)((float)gPlayer->bank_balance * 0.05f * (float)days_passed)));
					gPlayer->bank_balance += (int)((float)gPlayer->bank_balance * 0.05f * (float)days_passed);
				}
			}
			game_loop(gPlayer);
			days_passed = 0;
			break;
		case '2':
			unseen_msgs(gPlayer);
			if (interBBSMode == 1) {
				unseen_ibbs_msgs(gPlayer);
			}
			break;
		case '3':
			state_of_the_galaxy(gPlayer);
			od_printf("\r\nPress a key to continue\r\n");
			od_get_key(TRUE);
			break;
		case '4':
			od_send_file("scores.ans");
			od_printf("\r\nPress a key to continue\r\n");
			od_get_key(TRUE);
			break;
		case '5':
			for (i=0;i<InterBBSInfo.otherNodeCount;i++) {
				od_printf("`bright green`%s`white`\r\n", InterBBSInfo.otherNodes[i]->name);
			}
			od_printf("\r\nPress a key to continue\r\n");
			od_get_key(TRUE);
			break;
		case '6':
			od_send_file("ibbs_scores.ans");
			od_printf("\r\nPress a key to continue\r\n");
			od_get_key(TRUE);
			break;
		default:
			break;
		}
	} while (tolower(c) != 'q');
	free(gPlayer);
	od_exit(0, FALSE);
}

