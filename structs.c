#include<sys/socket.h>
#include<sys/types.h>
#include<netinet/ip.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include<string.h>
#include<pthread.h>
#include<signal.h>
#include<stdbool.h>

//The error codes and their meaning
	#define Other  0		//0	Other (not covered by any below error code)
	#define BadRoom  1		//1	Bad room. Attempt to change to an inappropriate room
	#define PlayerExist  2 		//2	Player Exists. Attempt to create a player that already exists.
	#define BadMonster  3		//3	Bad Monster. Attempt to loot a nonexistent or not present monster.
	#define StatErr  4		//4	Stat error. Caused by setting inappropriate player stats.
	#define NotReady  5		//5	Not Ready. Caused by attempting an action too early, for example changing rooms before sending START or CHARACTER.
	#define NoTarget  6		//6	No target. Sent in response to attempts to loot nonexistent players, fight players in different rooms, etc.
	#define NoFight  7		//7	No fight. Sent if the requested fight cannot happen for other reasons (i.e. no live monsters in room)
	#define NoPlayer  8		//8	No player vs. player combat on the server. Servers do not have to support player-vs-player combat.
//

	struct clinfo** client_list;
	size_t client_list_size = 0;
	size_t client_list_mem;
	pthread_mutex_t cl_mutex = PTHREAD_MUTEX_INITIALIZER;
	
	// rooms
	struct room** room_list;
    int broadcast(char[32], char *);
	
//

uint16_t Initial_Points = 200;
uint16_t Stat_limit = 65535;


//
void sendInfo(int skt){
	uint8_t type, major, minor;
	uint16_t ext_size;
	type = 14;
	major = 2;
	minor = 2;
	ext_size = 0;
	char buffer[1024];
	buffer[0]= type;
	buffer[1] = major;
	buffer[2] = minor;
	buffer[3] = ext_size;
	pthread_mutex_lock(&cl_mutex);
	if(write(skt, &buffer, 5) != 5)
		printf("impossible to send version\n");
	pthread_mutex_unlock(&cl_mutex);
}
//

typedef struct room{
    char* room_name;
    uint16_t room_number;
    char* room_description;
    struct room** connections;
    size_t connections_size;
    struct clinfo** residents;
    size_t residents_size;
    pthread_mutex_t room_mutex;

}room;
//
typedef struct clinfo {
	char ipaddr[16];
	int skt;
	char name[32];
	uint8_t flag; 
    bool monster;
    uint16_t Attack;
	uint16_t Defense;
	uint16_t Regen;
	int16_t Health;
	uint16_t Gold;
	uint16_t room_num; 
	uint16_t Description_length;
	char description[200];
}clinfo;

int sendGame(int skt){
    uint8_t Type = 11;
	char Game_description[1024];
    memcpy(Game_description, "In 2020, we thought we would be driving flying cars but Instead we got Covid 19. Now we need to find a safe country to stay in untill this is over. Good luck.\n there are 11 countries that are still enter a country(room number) under 12", 1024);
    uint16_t Description_Length = strlen(Game_description);
	pthread_mutex_lock(&cl_mutex);    
    if(write(skt, &Type, 1) != 1){
        printf("something went wrong in Send Game\n");
		pthread_mutex_unlock(&cl_mutex);
        return 1;
    }
    if(write(skt, &Initial_Points, 2) != 2){
        printf("something went wrong in Send Game\n");
		pthread_mutex_unlock(&cl_mutex);
        return 1;
    }
    if(write(skt, &Stat_limit, 2) != 2){
        printf("something went wrong in Send Game\n");
		pthread_mutex_unlock(&cl_mutex);
        return 1;
    }
    if(write(skt, &Description_Length, 2) != 2){
        printf("something went wrong in Send Game\n");
    	pthread_mutex_unlock(&cl_mutex);
        return 1;
    }
    if(write(skt, Game_description, Description_Length) != Description_Length){
        printf("something went wrong in Send Game\n");
		pthread_mutex_unlock(&cl_mutex);
        return 1;
    }
	pthread_mutex_unlock(&cl_mutex);
    return 1;
}

int send_error(int cfd, uint8_t code){
    uint8_t type = 7;
    if(code == PlayerExist){
        char* str = "Player Exists. Attempt to create a player that already exists.";
        uint16_t taille = strlen(str);
        uint8_t ercode = 2;
		pthread_mutex_lock(&cl_mutex);
        if(write(cfd, &type, 1) != 1)
            printf("error sending type for Player exists\n");
        if(write(cfd, &ercode, 1) != 1)
            printf("error sending ercode\n");
        if(write(cfd, &taille, 2) != 2)
            printf("could not send the size of the err mess for player exist\n");
		if(write(cfd, str, taille) != taille)
			printf("Could not send error %d\n", code);
		pthread_mutex_unlock(&cl_mutex);    
        return 0;
    }else if(code == StatErr){
        char* str = "Stat error. Your stat exceed the limit.";
        uint16_t taille = strlen(str);
        uint8_t ercode = 4;
		pthread_mutex_lock(&cl_mutex);
        if(write(cfd, &type, 1) != 1)
            printf("the type for stat eror is not gone\n");
        if(write(cfd, &ercode, 1) != 1)
            printf("error sending ercode2\n");
        if(write(cfd, &taille, 2) != 2)
            printf("could not send taille from stat err\n");
        if(write(cfd, str, taille) != taille)
            printf("failed to send err mess for stat\n");
	pthread_mutex_unlock(&cl_mutex);    
	return 0;
    }else if(code == Other){
       
        char* str = "Cannot make this move cause your character kinda dead or the other one is live\n";
        uint16_t taille = strlen(str);
		pthread_mutex_lock(&cl_mutex);
        if(write(cfd, &type, 1) != 1)
            printf("the type for stat eror is not gone\n");
        if(write(cfd, &code, 1) != 1)
            printf("error sending ercode2\n");
        if(write(cfd, &taille, 2) != 2)
            printf("could not send taille from stat err\n");
        if(write(cfd, str, taille) != taille)
            printf("failed to send err mess for stat\n");
		pthread_mutex_unlock(&cl_mutex);    
		return 0;
    }else if(code == NoTarget){
        char* str = "Cannot make this move cause your Target is in a different country if he exists at all!\n";
        uint16_t taille = strlen(str);
		pthread_mutex_lock(&cl_mutex);
        if(write(cfd, &type, 1) != 1)
            printf("the type for no Target eror is not gone\n");
        if(write(cfd, &code, 1) != 1)
            printf("error sending ercode no Target\n");
        if(write(cfd, &taille, 2) != 2)
            printf("could not send taille from no Target err\n");
        if(write(cfd, str, taille) != taille)
            printf("failed to send err mess for no Target\n");
	pthread_mutex_unlock(&cl_mutex);    
	return 0;
    }          
}

int send_accept(int cfd, uint8_t type){
    uint8_t typ = 8;
    pthread_mutex_lock(&cl_mutex);
    if(write(cfd, &typ, 1) != 1)
        printf("error sending 8\n");
    if(write(cfd, &type, 1) != 1)
        printf("could not send type \n");
    pthread_mutex_unlock(&cl_mutex);
    
}
//

int send_char(struct clinfo* subject, int personal_skt, uint16_t room_num){
        uint8_t type = 10;
	pthread_mutex_lock(&room_list[room_num]->room_mutex);
        if(write(personal_skt, &type, 1) != 1)
            printf("something failed sending type 10 char to %s\n", subject->name);
        if(write(personal_skt, &(subject->name), 32) != 32)
			printf("something was wrong getting the client name\n ");
		if(write(personal_skt, &(subject->flag), 1) != 1)
			printf("something was wrong getting the client Flag\n ");
		if(write(personal_skt, &(subject->Attack), 2) != 2)
			printf("something was wrong getting the client attack\n ");
		if(write(personal_skt, &(subject->Defense), 2) != 2)
			printf("something was wrong getting the client Defense\n ");
		if(write(personal_skt, &(subject->Regen), 2) != 2)
    		printf("something was wrong getting the client Regen\n ");
		if(write(personal_skt, &(subject->Health), 2) != 2)
		    printf("something was wrong getting the client Health\n ");
		if(write(personal_skt, &(subject->Gold), 2) != 2)
			printf("something was wrong getting the client Gold\n ");
		if(write(personal_skt, &(subject->room_num), 2) != 2)
			printf("something was wrong getting the client Room num\n ");
		if(write(personal_skt, &(subject->Description_length), 2) != 2)
			printf("something was wrong getting the client DESLEN\n ");
		if(write(personal_skt, subject->description, subject->Description_length) != subject->Description_length)
			printf("something was wrong getting the client Actual description\n ");
		pthread_mutex_unlock(&room_list[room_num]->room_mutex);
       
        return 0;
};

int send_characters(uint16_t room_num, size_t len){
    for (int i = 1; i < len; i++){
        if(room_list[room_num]->residents[i]->skt != 1) 
            for(int j = 0; j < len; j++)
                send_char(room_list[room_num]->residents[j], room_list[room_num]->residents[i]->skt , room_num);
    }
   
    return 0;
}
//

int send_connections(uint16_t room_num, int skt){
    uint16_t type = 13;
    for(int i=0; i < room_list[room_num]->connections_size; i++){
		pthread_mutex_lock(&room_list[room_num]->room_mutex);
        uint16_t room_des_len = strlen(room_list[room_num]->connections[i]->room_description);
        if(write(skt, &type, 1) != 1)
            printf("we could not send type 13(connection) in struct 200\n");
        if(write(skt, &room_list[room_num]->connections[i]->room_number, 2) != 2)
            printf("room number not sent\n");
        if(write(skt, room_list[room_num]->connections[i]->room_name, 32) != 32)
            printf("could not send connection name\n");
        if(write(skt, &room_des_len, 2) != 2)
            printf("could not send len of con des\n");
        if(write(skt, room_list[room_num]->connections[i]->room_description, room_des_len) != room_des_len)
            printf("could not send con des\n");
		pthread_mutex_unlock(&room_list[room_num]->room_mutex);    
    }
 
    return 0;
}

int rmv(uint16_t j, int i){
    
    struct clinfo *ctp = room_list[j]->residents[i];
    size_t clloc = 0;
    size_t clpos = 0;
	for(; clpos < client_list_size && client_list[clpos] != ctp; clpos++);
    pthread_mutex_lock(&cl_mutex);
	client_list[clpos] = client_list[--client_list_size ];
    pthread_mutex_unlock(&cl_mutex);
	for(; clloc < room_list[ctp->room_num]->residents_size && room_list[ctp->room_num]->residents[clloc] != ctp; clloc++);
    pthread_mutex_lock(&cl_mutex);
	room_list[ctp->room_num]->residents[clloc] = room_list[ctp->room_num]->residents[--room_list[ctp->room_num]->residents_size];
    pthread_mutex_unlock(&cl_mutex);
	broadcast(ctp->name	, "Left");
	
}
int broadcast(char name[32], char* action){
    char string[128];
    uint16_t type = 1;
    char Na[32];
    memset(Na, 0, 32);
    strcpy(Na, "Narrator");
    uint16_t len = sprintf(string, "%s had %s .",name, action, 128);
    for(uint16_t j = 0; j <= 12; j++){
        for(int i = 0; i < room_list[j]->residents_size; i++){
        	pthread_mutex_lock(&cl_mutex);
            if(write(room_list[j]->residents[i]->skt, &type, 1) != 1){
	        pthread_mutex_unlock(&cl_mutex);
                rmv(j,i);
                printf("could not send message type\n");
                return 0;
            }
            if(write(room_list[j]->residents[i]->skt, &len, 2) != 2)
                printf("could not send msg len\n");
            if(write(room_list[j]->residents[i]->skt, &room_list[i]->residents[i]->name, 32) != 32)
                printf("could not send msg len\n");
            if(write(room_list[j]->residents[i]->skt, Na, 32) != 32)
                printf("could not send msg len\n");    
            if(write(room_list[j]->residents[i]->skt, string, len) != len)
                printf("could not send the msg\n");
            pthread_mutex_unlock(&cl_mutex);
            }
        }
	
    return 0;
}
int send_room(uint16_t room_num, int socket){
    	uint8_t type = 9;
	struct room *loc = room_list[room_num]; 
	uint16_t deslen = strlen(loc->room_description);
    	pthread_mutex_lock(&room_list[room_num]->room_mutex);
    	if(write(socket, &type, 1) != 1)
        	printf("could not send type 9 %d \n", socket);
    	if(write(socket, &room_num, 2) != 2)
		printf("we could not send the room num\n");
	if(write(socket, loc->room_name, 32) != 32)
		printf("we could not send the name of the room\n");
	if(write(socket, &deslen, 2) != 2)
		printf("could not send the deslen\n");
	if(write(socket, loc->room_description, deslen) != deslen)
		printf("could not send actual description\n");
    	pthread_mutex_unlock(&room_list[room_num]->room_mutex);
	return 0;
}
int update_room(uint16_t room_dest, size_t len){
    for(int i= 1; i < room_list[room_dest]->residents_size; i++){
        if(room_list[room_dest]->residents[i]->skt != 1){
            send_room(room_dest, room_list[room_dest]->residents[i]->skt);
        }else{continue;}
    }
    send_characters(room_dest, len);

    for(int i= 1; i < room_list[room_dest]->residents_size; i++){
        if(room_list[room_dest]->residents[i]->skt != 1)
            send_connections(room_dest, room_list[room_dest]->residents[i]->skt);
    }    
   
    return 0;
}











