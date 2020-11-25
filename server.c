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
#include "structs.c"
#include "errno.h"
int skt;
int tp = 0;

void stop_server(int signum){
	close(skt);
	if(signum == SIGSEGV)
		printf("Received signal 11, SIGSEGV!\n");
	exit(0);
}
//

void pipe_fn(int signal){
	time_t now = time(0);
	printf("Broken pipe at %s\n", ctime(&now));
}
//
int find_username(char* uname, char* ip){
	printf("finding username");
	pthread_mutex_lock(&cl_mutex);
	for(size_t i = 0; i < client_list_size; i++){
		if(!strncmp(client_list[i]->name, uname, 32) && strncmp(client_list[i]->name, ip, 16)){
			pthread_mutex_unlock(&cl_mutex);
			return client_list[i]->skt;
		}
	}
	pthread_mutex_unlock(&cl_mutex);
	printf("found/ username");
	return 0;
}

int find_skt(char* uname){
	for(size_t i = 0; i < client_list_size; i++)
		if(!strncmp(client_list[i]->name, uname, 32))
			return client_list[i]->skt;
	return 0;
}


#define BUFSIZE (1024*64)
void* client_thread(void* param){
	struct clinfo *ctp = (struct clinfo*)param;
	pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
	// ctp->personal_mutex =mtx;
	char buffer[BUFSIZE];
	int cfd = ctp->skt;
	size_t readlen;
	uint16_t type = 0;
	sendInfo(cfd);
	sendGame(cfd);
	for(;;){
		if(recv(cfd, &type, 1, MSG_WAITALL) != 1){
			printf(" %s: Probleme reading the type\n", ctp->name);
			goto bad_problem;
		}
		if(type == 1){
			uint16_t mlen;
			char rec[32];
			char send[32];
			char msg[128];
            		if(recv(cfd, &mlen, 2, MSG_WAITALL) != 2)
                		printf("could not get msg len\n");
            		if(recv(cfd, &rec, 32, MSG_WAITALL) != 32)
               			printf("could not get recipient\n");
            		if(recv(cfd, &send, 32, MSG_WAITALL) != 32)
                		printf("could not get msg destinataire\n");    
            		if(recv(cfd, &msg, mlen, MSG_WAITALL) != mlen)
                		printf("could not get the msg\n");
			int fd = find_skt(rec);
			if(!fd){
				send_error(cfd, NoTarget);
			}else{
				pthread_mutex_lock(&cl_mutex);
				if(write(fd, &type, 1) != 1)
					printf("could not send message type\n");
				if(write(fd, &mlen, 2) != 2)
					printf("could not send msg len\n");
				if(write(fd, rec, 32) != 32)
					printf("could not send msg len\n");
				if(write(fd, send, 32) != 32)
					printf("could not send msg len\n");    
				if(write(fd, msg, mlen) != mlen)
					printf("could not send the msg\n");
				pthread_mutex_unlock(&cl_mutex);
			}
		}
		if(type == 2){
			
			uint16_t destRoom;
			if(recv(cfd, &destRoom, 2, MSG_WAITALL) != 2){
				printf("could not get the destination room\n");
			}
			int respos = 0;
			uint16_t old_room = ctp->room_num;
			for(; respos < room_list[ctp->room_num]->residents_size && client_list[respos] != ctp; respos++);
			room_list[ctp->room_num]->residents[respos] = room_list[ctp->room_num]->residents[--room_list[ctp->room_num]->residents_size];
			room_list[destRoom]->residents[(room_list[destRoom]->residents_size)++] = ctp;
			ctp->room_num = destRoom;
			update_room(old_room, room_list[old_room]->residents_size);
			update_room(destRoom, room_list[destRoom]->residents_size);
		}
		if(type == 3){ //Player VS Monster
			//check if the player calling fight can fight
			if(!(ctp->flag & 128))
				send_error(cfd, 0);
			else{
				printf("%s has started fighting\n", ctp->name);
				uint16_t monster_damage, client_damage;
				
				monster_damage = (room_list[ctp->room_num]->residents[tp]->Attack)/10;
				client_damage = (ctp->Attack) / 10;
				pthread_mutex_lock(&room_list[ctp->room_num]->room_mutex);
				ctp->Health -=  monster_damage;
				room_list[ctp->room_num]->residents[tp]->Health -= client_damage;
				
				if(ctp->Health < 0){
					ctp-> flag = ctp->flag & (!128) ;
					pthread_mutex_unlock(&room_list[ctp->room_num]->room_mutex);
					send_characters(ctp->room_num, room_list[ctp->room_num]->residents_size);
					goto done;
				}
				if(room_list[ctp->room_num]->residents[tp]->Health <= 0){
					ctp->Gold += room_list[ctp->room_num]->residents[tp]->Gold;
					room_list[ctp->room_num]->residents[tp]->flag = room_list[ctp->room_num]->residents[tp]->flag & (!128) ;
					pthread_mutex_unlock(&room_list[ctp->room_num]->room_mutex);
					room_list[ctp->room_num]->residents[tp]->Health = 100;
					room_list[ctp->room_num]->residents[tp]->flag = room_list[ctp->room_num]->residents[tp]->flag | (128);
				}
				pthread_mutex_unlock(&room_list[ctp->room_num]->room_mutex);
				update_room(ctp->room_num, room_list[ctp->room_num]->residents_size);
				
				if(tp == 1){
					tp = 0;
				}else{
					tp = 1;
				}
				printf("%s has finished fighting\n", ctp->name);
			}
		}
		if(type == 4){//PVP
			if(!(ctp->flag & 128)){
				send_error(cfd, 0);
			}
			else{
				char target[32];
				if(recv(cfd, &target, 32, MSG_WAITALL) != 32)
					printf("did not receive a target \n");
			
				int respos = 0;
				for(; respos < room_list[ctp->room_num]->residents_size; respos++){
					if(!strcmp(room_list[ctp->room_num]->residents[respos]->name, target))
						break;
				}
				if(respos == 0){
					send_error(cfd, 6);
					continue;
				}
				
				struct clinfo *adversaire = client_list[respos];
				uint16_t adversaire_damage, client_damage;
				
				adversaire_damage = (room_list[ctp->room_num]->residents[respos]->Attack)/10;
				client_damage = (ctp->Attack) / 10;
				ctp->Health -=  adversaire_damage;
				pthread_mutex_lock(&room_list[ctp->room_num]->room_mutex);	
				room_list[ctp->room_num]->residents[respos]->Health -= client_damage;
				
				if(ctp->Health < 0){
		
					ctp-> flag = ctp->flag & (!128) ;
					pthread_mutex_unlock(&room_list[ctp->room_num]->room_mutex);
					update_room(ctp->room_num, room_list[ctp->room_num]->residents_size);
					usleep(500);//wait for a bit so other players can loot him
					goto done;
				}
				if(room_list[ctp->room_num]->residents[respos]->Health <= 0){
					ctp->Gold += room_list[ctp->room_num]->residents[respos]->Gold;
					room_list[ctp->room_num]->residents[respos]->flag = room_list[ctp->room_num]->residents[respos]->flag & (!128) ;
				}
				pthread_mutex_unlock(&room_list[ctp->room_num]->room_mutex);
				update_room(ctp->room_num, room_list[ctp->room_num]->residents_size);
			
			}			
		}
		if(type == 5){//Loot
			char target[32];
			if(recv(cfd, &target, 32, MSG_WAITALL) != 32)
				printf("did not receive the taget \n");
			int respos = 0;
			for(; respos < room_list[ctp->room_num]->residents_size; respos++)
				if(!strcmp(room_list[ctp->room_num]->residents[respos]->name, target))
					break;
			if(respos == 0){
				send_error(cfd, 6);
				continue;
			}
			if(room_list[ctp->room_num]->residents[respos]->Health > 0)
						send_error(cfd, 0);
			if(room_list[ctp->room_num]->residents[respos]->Health <= 0){
				ctp->Gold += room_list[ctp->room_num]->residents[respos]->Gold;
				room_list[ctp->room_num]->residents[respos]->Gold = 0;
			}

		}
		if(type == 6){
			// ctp->flag = ((ctp->flag | 24) | 128);

			ctp->flag = 216;
			printf("%d \n", ctp->flag);
			ctp->room_num = 0;
			ctp->monster = false;
			ctp->Health = 100;
			client_list[client_list_size++] = ctp;
			room_list[ctp->room_num]->residents[room_list[ctp->room_num]->residents_size++] = ctp;
			send_accept(cfd, 6);
			send_room(ctp->room_num, ctp->skt);
			send_characters(ctp->room_num, room_list[ctp->room_num]->residents_size);
			send_connections(ctp->room_num, ctp->skt);
		}
		if(type== 10){
			
			if(recv(cfd, ctp->name, 32, MSG_WAITALL) != 32)
				printf("something was wrong getting the client name\n ");
			if(recv(cfd, &(ctp->flag), 1, MSG_WAITALL) != 1)
				printf("something was wrong getting the client Flag\n ");
			if(recv(cfd, &(ctp->Attack), 2, MSG_WAITALL) != 2)
				printf("something was wrong getting the client attack\n ");
			if(recv(cfd, &(ctp->Defense), 2, MSG_WAITALL) != 2)
				printf("something was wrong getting the client Defense\n ");
			if(recv(cfd, &(ctp->Regen), 2, MSG_WAITALL) != 2)
				printf("something was wrong getting the client Regen\n ");
			if(recv(cfd, &(ctp->Health), 2, MSG_WAITALL) != 2)
				printf("something was wrong getting the client Health\n ");
			if(recv(cfd, &(ctp->Gold), 2, MSG_WAITALL) != 2)
				printf("something was wrong getting the client Gold\n ");
			if(recv(cfd, &(ctp->room_num), 2, MSG_WAITALL) != 2)
				printf("something was wrong getting the client Room num\n ");
			if(recv(cfd, &(ctp->Description_length), 2, MSG_WAITALL) != 2)
				printf("something was wrong getting the client DESLEN\n ");
			if(recv(cfd, ctp->description, ctp->Description_length, MSG_WAITALL) != ctp->Description_length)
				printf("something was wrong getting the client Actual description\n ");

			if(find_username(ctp->name, ctp->ipaddr)){
				printf("something wrong with the username\n");
				send_error(cfd,PlayerExist);
				printf(" sent the error message for uname\n");
			}
			if ((ctp->Attack + ctp->Regen + ctp->Defense) > Initial_Points){
				printf("something wrong with the Stats\n");
				send_error(cfd, StatErr);
				printf(" sent the error message for stats\n");
				
			}else{
				printf("He Gucci\n");
				send_accept(cfd, type);
				send_char(ctp, cfd, 0);
			}
		}
		if(type == 12){
			printf("Client %s, ip %s is leaving the game!\n", ctp->name, ctp->ipaddr);
			goto done;
		}
	}
bad_problem:
	printf("Bad problem with client %s, ip %s\n", ctp->name, ctp->ipaddr);
done:
	printf("getting done\n");
	// Note:  This is NOT thread safe
	// It should be!  
	// At some point, probably closer to server time, we'll fix it
	size_t clpos = 0;
	size_t	clloc = 0;
	pthread_mutex_lock(&cl_mutex);
	close(cfd);
	for(; clpos < client_list_size && client_list[clpos] != ctp; clpos++);
	client_list[clpos] = client_list[--client_list_size ];
	for(; clloc < room_list[ctp->room_num]->residents_size && room_list[ctp->room_num]->residents[clloc] != ctp; clloc++);
	room_list[ctp->room_num]->residents[clloc] = room_list[ctp->room_num]->residents[--room_list[ctp->room_num]->residents_size];
	free(ctp);
	pthread_mutex_unlock(&cl_mutex);

	printf("All done with %s, client_list_size == %lu\n", ctp->name, client_list_size);
	
	return 0;
}

int main(int argc, char ** argv){
		struct clinfo *Kabila, *Biya, *Lourenco, *Buhari, *Saied, *Ahmad, *Zewde, *Deby, *Gnassingbe, *Issoufou, *alBashir, *Kagame, *Museveni, *Crocodile, *lionhead;
		//create monsters
		Kabila = (struct clinfo*)calloc(sizeof(struct clinfo), 1);
		char *z = "0";
		strcpy(Kabila->ipaddr , z);
		Kabila->skt = 1;
		strcpy(Kabila->name , "Kabila Kabange");
		Kabila->flag = 184;
		Kabila->monster = true;
		Kabila->Attack = 50;
		Kabila->Defense = 100;
		Kabila->Regen = 10;
		Kabila->Health = 100;
		Kabila->Gold = 243;
		Kabila->room_num = 0;
	
		strcpy(Kabila->description , "President of the DRC. careful with him, he's got Bana Mura");
		Kabila->Description_length;
		//
		Biya = (struct clinfo*)calloc(sizeof(struct clinfo), 1);
		Biya->ipaddr[0] = 0;
		Biya->skt = 1;
		strcpy( Biya->name,"Biya");
		Biya->flag = 184;
		Biya->monster = true;
		Biya->Attack = 100;
		Biya->Defense = 60;
		Biya->Regen = 100;
		Biya->Health = 80;
		Biya->Gold = 70;
		Biya->room_num = 1;
		strcpy( Biya->description, "Paul Biya is a Cameroonian politician serving as the President of Cameroon since 6 November 1982.");
		Biya->Description_length = strlen(Biya->description);
		//
		Lourenco = (struct clinfo*)calloc(sizeof(struct clinfo), 1);
		strcpy(Lourenco->ipaddr , "0");
		Lourenco->skt = 1;

		strcpy(Lourenco->name , "Lourenco");
		Lourenco->flag = 184;
		Lourenco->monster = true;
		Lourenco->Attack = 100;
		Lourenco->Defense = 60;
		Lourenco->Regen = 100;
		Lourenco->Health = 80;
		Lourenco->Gold = 70;
		Lourenco->room_num = 2;
	
		strcpy(Lourenco->description , "João Manuel Gonçalves Lourenco, GColIH is an Angolan politician who has served as the President of Angola since 26 September 2017.");
		Lourenco->Description_length = strlen(Lourenco->description);
		//
		Buhari = (struct clinfo*)calloc(sizeof(struct clinfo), 1);
		strcpy(Buhari->ipaddr , "0");
		Buhari->skt = 1;

		strcpy(Buhari->name , "Buhari");
		Buhari->flag = 184;
		Buhari->monster = true;
		Buhari->Attack = 100;
		Buhari->Defense = 60;
		Buhari->Regen = 100;
		Buhari->Health = 80;
		Buhari->Gold = 70;
		Buhari->room_num = 3;

		strcpy(Buhari->description , "Muhammadu Buhari GCFR is a Nigerian politician currently serving as the President of Nigeria, in office since 2015.");
		Buhari->Description_length = strlen(Buhari->description);
		//
		Saied = (struct clinfo*)calloc(sizeof(struct clinfo), 1);
		strcpy(Saied->ipaddr , "0");
		Saied->skt = 1;

		strcpy(Saied->name ,  "Saied");
		Saied->flag = 184;
		Saied->monster = true;
		Saied->Attack = 100;
		Saied->Defense = 60;
		Saied->Regen = 100;
		Saied->Health = 80;
		Saied->Gold = 70;
		Saied->room_num = 4;

		strcpy(Saied->description , "Kais Saied is a Tunisian politician, statesman, jurist and former lecturer serving as the fifth President of Tunisia since October 2019.");
		Saied->Description_length = strlen(Saied->description);
		//
		Ahmad = (struct clinfo*)calloc(sizeof(struct clinfo), 1);
		strcpy(Ahmad->ipaddr , "0");
		Ahmad->skt = 1;
	
		strcpy(Ahmad->name , "Touadéra");
		Ahmad->flag = 184;
		Ahmad->monster = true;
		Ahmad->Attack = 100;
		Ahmad->Defense = 60;
		Ahmad->Regen = 100;
		Ahmad->Health = 80;
		Ahmad->Gold = 70;
		Ahmad->room_num = 5;
	
		strcpy(Ahmad->description , "Faustin-Archange Touadéra is a Central African politician and academic who has been President of the Central African Republic since March 2016.");
		Ahmad->Description_length = strlen(Ahmad->description);
		//
		Zewde = (struct clinfo*)calloc(sizeof(struct clinfo), 1);
		strcpy(Zewde->ipaddr , "0");
		Zewde->skt = 1;
	
		strcpy(Zewde->name , "Zwede");
		Zewde->flag = 184;
		Zewde->monster = true;
		Zewde->Attack = 100;
		Zewde->Defense = 60;
		Zewde->Regen = 100;
		Zewde->Health = 80;
		Zewde->Gold = 70;
		Zewde->room_num = 6;
	
		strcpy(Zewde->description, "Sahle-Work Zewde is the current President of Ethiopia and the first woman to hold the office.");
		Zewde->Description_length = strlen(Zewde->description);
		//
		Deby = (struct clinfo*)calloc(sizeof(struct clinfo), 1);
		strcpy(Deby->ipaddr, "0");
		Deby->skt = 1;
	
		strcpy(Deby->name, "Deby");
		Deby->flag = 184;
		Deby->monster = true;
		Deby->Attack = 100;
		Deby->Defense = 60;
		Deby->Regen = 100;
		Deby->Health = 80;
		Deby->Gold = 70;
		Deby->room_num = 7;
	
		strcpy(Deby->description, "General Idriss Deby Itno is a Chadian politician who has been the President of Chad since 1990.");
		Deby->Description_length = strlen(Deby->description);
		//
		Gnassingbe = (struct clinfo*)calloc(sizeof(struct clinfo), 1);
		strcpy(Gnassingbe->ipaddr, "0");
		Gnassingbe->skt = 1;
	
		strcpy(Gnassingbe->name, "Gnassingbe");
		Gnassingbe->flag = 184;
		Gnassingbe->monster = true;
		Gnassingbe->Attack = 100;
		Gnassingbe->Defense = 60;
		Gnassingbe->Regen = 100;
		Gnassingbe->Health = 80;
		Gnassingbe->Gold = 70;
		Gnassingbe->room_num = 8;
	
		strcpy(Gnassingbe->description, "Faure Essozimna Gnassingbe Eyadéma is a Togolese politician who has been the President of Togo since 2005. ");
		Gnassingbe->Description_length = strlen(Gnassingbe->description);
		//
		Issoufou = (struct clinfo*)calloc(sizeof(struct clinfo), 1);
		strcpy(Issoufou->ipaddr, "0");
		Issoufou->skt = 1;
	
		strcpy(Issoufou->name, "Issoufou");
		Issoufou->flag = 184;
		Issoufou->monster = true;
		Issoufou->Attack = 100;
		Issoufou->Defense = 60;
		Issoufou->Regen = 100;
		Issoufou->Health = 80;
		Issoufou->Gold = 70;
		Issoufou->room_num = 9;
	
		strcpy(Issoufou->description, "Mahamadou Issoufou is a Nigerien politician who has been the President of Niger since 7 April 2011.");
		Issoufou->Description_length = strlen(Issoufou->description);
		//
		alBashir = (struct clinfo*)calloc(sizeof(struct clinfo), 1);
		strcpy(alBashir->ipaddr, "0");
		alBashir->skt = 1;
	
		strcpy(alBashir->name, "alBashir");
		alBashir->flag = 184;
		alBashir->monster = true;
		alBashir->Attack = 100;
		alBashir->Defense = 60;
		alBashir->Regen = 100;
		alBashir->Health = 80;
		alBashir->Gold = 70;
		alBashir->room_num = 10;
	
		alBashir->description;
		alBashir->Description_length = strlen(alBashir->description);
		//
		Kagame = (struct clinfo*)calloc(sizeof(struct clinfo), 1);
		strcpy(Kagame->ipaddr, "0");
		Kagame->skt = 1;
	
		strcpy(Kagame->name, "Kagame");
		Kagame->flag = 184;
		Kagame->monster = true;
		Kagame->Attack = 100;
		Kagame->Defense = 60;
		Kagame->Regen = 100;
		Kagame->Health = 80;
		Kagame->Gold = 70;
		Kagame->room_num = 11;
		strcpy(Kagame->description, "Paul Kagame is a Rwandan politician and former military leader. He is the 4th and current President of Rwanda, having taken office in 2000 when his predecessor, Pasteur Bizimungu, resigned.");
		Kagame->Description_length = strlen(Kagame->description);
		//
		Museveni = (struct clinfo*)calloc(sizeof(struct clinfo), 1);
		strcpy(Museveni->ipaddr, "0");
		Museveni->skt = 1;
	
		strcpy(Museveni->name, "Museveni");
		Museveni->flag = 184;
		Museveni->monster = true;
		Museveni->Attack = 100;
		Museveni->Defense = 60;
		Museveni->Regen = 100;
		Museveni->Health = 80;
		Museveni->Gold = 70;
		Museveni->room_num = 12;
	
		strcpy(Museveni->description, "Yoweri Kaguta Museveni is a Ugandan politician who has been President of Uganda since 1986.");
		Museveni->Description_length = strlen(Museveni->description);
		//
		Crocodile = (struct clinfo*)calloc(sizeof(struct clinfo), 1);
		strcpy(Crocodile->ipaddr, "0");
		Crocodile->skt = 1;
	
		strcpy(Crocodile->name,"Crocohead");
		Crocodile->flag = 184;
		Crocodile->monster = true;
		Crocodile->Attack = 100;
		Crocodile->Defense = 60;
		Crocodile->Regen = 100;
		Crocodile->Health = 80;
		Crocodile->Gold = 70;
		Crocodile->room_num = 1;
		//char [] = "";
		strcpy(Crocodile->description,"Crocodiles or true crocodiles are large semiaquatic reptiles that live throughout the tropics in Africa, Asia, the Americas and Australia.");
		Crocodile->Description_length = strlen(Crocodile->description);
		//
		lionhead = (struct clinfo*)calloc(sizeof(struct clinfo), 1);
		strcpy(lionhead->ipaddr,"0");
		lionhead->skt = 1;
	
		strcpy(lionhead->name, "Chimera");
		lionhead->flag = 184;
		lionhead->monster = true;
		lionhead->Attack = 100;
		lionhead->Defense = 60;
		lionhead->Regen = 100;
		lionhead->Health = 80;
		lionhead->Gold = 70;
		lionhead->room_num = 4;
		strcpy(lionhead->description, "A genetic chimerism or chimera is a single organism composed of cells with more than one distinct genotype.");
		
		lionhead->Description_length = strlen(lionhead->description);
		//
		//list of countries and details
		printf("started with the coundtries\n");
		struct room *DRC = (struct room*)calloc(sizeof(struct room), 1);
		DRC->connections = (struct room**)malloc(sizeof(void*)*6);
		DRC->residents = (struct clinfo**)malloc(sizeof(void*)*1024);
		DRC->residents[0] = Kabila;
		DRC->residents_size = 1;
		struct room *Cameroon = (struct room*)calloc(sizeof(struct room), 1);
		Cameroon->connections = (struct room**)malloc(sizeof(void*)*6);
		Cameroon->residents = (struct clinfo**)malloc(sizeof(void*)*1024);
		Cameroon->residents[0] = Biya;
		Cameroon->residents_size = 1;
		struct room *Angola = (struct room*)calloc(sizeof(struct room), 1);
		Angola->connections = (struct room**)malloc(sizeof(void*)*6);
		Angola->residents = (struct clinfo**)malloc(sizeof(void*)*1024);
		Angola->residents[0] = Lourenco;
		Angola->residents_size = 1;
		struct room *Nigeria = (struct room*)calloc(sizeof(struct room), 1);
		Nigeria->connections = (struct room**)malloc(sizeof(void*)*6);
		Nigeria->residents = (struct clinfo**)malloc(sizeof(void*)*1024);
		Nigeria->residents[0] = Buhari;
		Nigeria->residents[1] = lionhead;
		Nigeria->residents_size = 2;
		struct room *Tunisia = (struct room*)calloc(sizeof(struct room), 1);
		Tunisia->connections = (struct room**)malloc(sizeof(void*)*6);
		Tunisia->residents = (struct clinfo**)malloc(sizeof(void*)*1024);
		Tunisia->residents[0] = Saied;
		Tunisia->residents_size = 1;
		struct room *RCA = (struct room*)calloc(sizeof(struct room), 1);
		RCA->connections = (struct room**)malloc(sizeof(void*)*6);
		RCA->residents = (struct clinfo**)malloc(sizeof(void*)*1024);
		RCA->residents[0] = Ahmad;
		RCA->residents_size = 1;
		struct room *Ethiopia = (struct room*)calloc(sizeof(struct room), 1);
		Ethiopia->connections = (struct room**)malloc(sizeof(void*)*6);
		Ethiopia->residents = (struct clinfo**)malloc(sizeof(void*)*1024);
		Ethiopia->residents[0] = Zewde;
		Ethiopia->residents_size = 1;
		struct room *Chad = (struct room*)calloc(sizeof(struct room), 1);
		Chad->connections = (struct room**)malloc(sizeof(void*)*6);
		Chad->residents = (struct clinfo**)malloc(sizeof(void*)*1024);
		Chad->residents[0] = Deby;
		Chad->residents_size = 1;
		struct room *Togo = (struct room*)calloc(sizeof(struct room), 1);
		Togo->connections = (struct room**)malloc(sizeof(void*)*6);
		Togo->residents = (struct clinfo**)malloc(sizeof(void*)*1024);
		Togo->residents[0] = Gnassingbe;
		Togo->residents_size = 1;
		struct room *Niger = (struct room*)calloc(sizeof(struct room), 1);
		Niger->connections = (struct room**)malloc(sizeof(void*)*6);
		Niger->residents = (struct clinfo**)malloc(sizeof(void*)*1024);
		Niger->residents[0] = Issoufou;
		Niger->residents[1] = Crocodile;
		Niger->residents_size = 2;
		struct room *Sudan = (struct room*)calloc(sizeof(struct room), 1);
		Sudan->connections = (struct room**)malloc(sizeof(void*)*6);
		Sudan->residents = (struct clinfo**)malloc(sizeof(void*)*1024);
		Sudan->residents[0] = alBashir;
		Sudan->residents_size = 1;
		struct room *Rwanda = (struct room*)calloc(sizeof(struct room), 1);
		Rwanda->connections = (struct room**)malloc(sizeof(void*)*6);
		Rwanda->residents = (struct clinfo**)malloc(sizeof(void*)*1024);
		Rwanda->residents[0] = Kagame;
		Rwanda->residents_size = 1;
		struct room *Uganda = (struct room*)calloc(sizeof(struct room), 1);
		Uganda->connections = (struct room**)malloc(sizeof(void*)*6);
		Uganda->residents = (struct clinfo**)malloc(sizeof(void*)*1024);
		Uganda->residents[0] = Museveni;
		Uganda->residents_size = 1;
		//Room details
		DRC->room_name = "Democratic Republic of Congo";
		DRC->room_number = 0;
		DRC->room_description = "The Democratic Republic of the Congo, also known as DR Congo, the DRC, DROC, Congo-Kinshasa, or simply the Congo, is a country located in Central Africa. It was formerly called Zaire. It is, by area, the largest country in sub-Saharan Africa, the second-largest in all of Africa, and the 11th-largest in the world";
		DRC->connections[0] = Uganda;
		DRC->connections[1] = Rwanda;
		DRC->connections[2] = RCA;
		DRC->connections[3] = Angola;
		DRC->connections_size = 4;
		//Cameroon
		Cameroon->room_name = "Cameroon";
		Cameroon->room_number = 1;
		Cameroon->room_description = "Cameroon, on the Gulf of Guinea, is a Central African country of varied terrain and wildlife-> Its inland capital, Yaoundé, and its biggest city, the seaport Douala, are transit points to ecotourism sites as well as beach resorts like Kribi – near the Chutes de la Lobé waterfalls, which plunge directly into the sea – and Limbe, where the Limbe Wildlife Centre houses rescued primates.";
		Cameroon->connections[0] = RCA; 
		Cameroon->connections[1] = Nigeria;
		Cameroon->connections[2]= DRC;
		Cameroon->connections_size = 3;
		//Angola
		Angola->room_name = "Angola";
		Angola->room_number = 2;
		Angola->room_description = "Angola is a Southern African nation whose varied terrain encompasses tropical Atlantic beaches, a labyrinthine system of rivers and Sub-Saharan desert that extends across the border into Namibia. The country's colonial history is reflected in its Portuguese-influenced cuisine and its landmarks including Fortaleza de Sao Miguel, a fortress built by the Portuguese in 1576 to defend the capital, Luanda";
		Angola->connections[0] = DRC;
		Angola->connections_size = 1;
		//Nigeria
		Nigeria->room_name = "Nigeria";
		Nigeria->room_number = 3;
		Nigeria->room_description = "Nigeria, an African country on the Gulf of Guinea, has many natural landmarks and wildlife reserves-> Protected areas such as Cross River National Park and Yankari National Park have waterfalls, dense rainforest, savanna and rare primate habitats. One of the most recognizable sites is Zuma Rock, a 725m-tall monolith outside the capital of Abuja that’s pictured on the national currency.";
		Nigeria->connections[0]= Chad;
		Nigeria->connections[1]= Cameroon;
		Nigeria->connections_size = 2;
		//Tunisia
		Tunisia->room_name = "Tunisia";
		Tunisia->room_number =4 ;
		Tunisia->room_description = "Tunisia is a North African country bordering the Mediterranean Sea and Sahara Desert-> In the capital, Tunis, the Bardo Museum has archaeological exhibits from Roman mosaics to Islamic art. The city’s medina quarter encompasses the massive Al-Zaytuna Mosque and a thriving souk. To the east, the site of ancient Carthage features the Antonine Baths and other ruins, plus artifacts at the Carthage National Museum.";
		Tunisia->connections[0] = Niger;
		Tunisia->connections_size = 1;
		//RCA
		RCA->room_name = "Republique Centre-Africaine";
		RCA->room_number = 5;
		RCA->room_description = "The Central African Republic is a landlocked country in Central Africa-> It is bordered by Chad to the north, Sudan to the northeast, South Sudan to the southeast, the Democratic Republic of the Congo to the south, the Republic of the Congo to the southwest and Cameroon to the west.";
		RCA->connections[0] = DRC;
		RCA->connections[1] = Cameroon;
		RCA->connections[2] = Sudan;
		RCA->connections_size = 3;
		//Ethiopia
		Ethiopia->room_name = "Ethiopia";
		Ethiopia->room_number = 6;
		Ethiopia->room_description = "Ethiopia, in the Horn of Africa, is a rugged, landlocked country split by the Great Rift Valley-> With archaeological finds dating back more than 3 million years, it’s a place of ancient culture. Among its important sites are Lalibela with its rock-cut Christian churches from the 12th–13th centuries. Aksum is the ruins of an ancient city with obelisks, tombs, castles and Our Lady Mary of Zion church.";
		Ethiopia->connections[0] = Sudan;
		Ethiopia->connections_size = 1;
		//Chad
		Chad->room_name = "Chad";
		Chad->room_number = 7;
		Chad->room_description = "Chad, officially known as the Republic of Chad, is a landlocked country in north-central Africa-> It is bordered by Libya to the north, Sudan to the east, the Central African Republic to the south, Cameroon to the south-west, Nigeria to the southwest, and Niger to the west.";
		Chad->connections[0] = Sudan; 
		Chad->connections[1] = Niger;
		Chad->connections[2] = RCA;
		Chad->connections[3] = Cameroon; 
		Chad->connections[4] = Nigeria;
		Chad->connections_size = 5;
		//Togo
		Togo->room_name = "Togo";
		Togo->room_number = 8;
		Togo->room_description = "Togo, a West African nation on the Gulf of Guinea, is known for its palm-lined beaches and hilltop villages. Koutammakou, inhabited by the Batammariba people, is a traditional settlement of fortresslike clay huts dating to the 17th century. In the capital, Lomé, are the multistory Grand Marché bazaar and the Fetish Market, offering traditional talismans and remedies relating to the vodun (voodoo) religion.";
		Togo->connections[0] = Nigeria, 
		Togo->connections[1] = Niger;
		Togo->connections_size = 2;
		//Niger
		Niger->room_name = "Niger";
		Niger->room_number = 9;
		Niger->room_description = "Niger or the Niger, officially the Republic of the Niger, is a landlocked country in West Africa named after the Niger River.";
		Niger->connections[0] = Chad;
		Niger->connections[1] = Nigeria;
		Niger->connections_size = 2;
		//Sudan
		Sudan->room_name = "Sudan";
		Sudan->room_number =10 ;
		Sudan->room_description = "Sudan officially the Republic of the Sudan, it is a country in Northeast Africa it is bordered by Egypt to the north, Libya to the northwest, Chad to the west, the Central African Republic to the southwest, South Sudan to the south, Ethiopia to the southeast, Eritrea to the east, and the Red Sea to the northeast.";
		Sudan->connections[0] = Chad;
		Sudan->connections[1] = RCA;
		Sudan->connections[2] = Ethiopia;
		Sudan->connections[3] = DRC;
		Sudan->connections[4] = Uganda;
		Sudan->connections_size = 5;
		//Rwanda
		Rwanda->room_name = "Rwanda";
		Rwanda->room_number =11 ;
		Rwanda->room_description = "Rwanda, formerly Ruanda, officially the Republic of Rwanda, is a landlocked country in the Great Rift Valley where the African Great Lakes region and East Africa converge. One of the smallest countries on the African mainland, its capital city is Kigali.";
		Rwanda->connections[0] = DRC; 
		Rwanda->connections[1] = Uganda;
		Rwanda->connections_size = 2;
		//Uganda
		Uganda->room_name = "Uganda";
		Uganda->room_number =12 ;
		Uganda->room_description = "Uganda is a landlocked country in East Africa whose diverse landscape encompasses the snow-capped Rwenzori Mountains and immense Lake Victoria. Its abundant wildlife includes chimpanzees as well as rare birds. Remote Bwindi Impenetrable National Park is a renowned mountain gorilla sanctuary. Murchison Falls National Park in the northwest is known for its 43m-tall waterfall and wildlife such as hippos.";
		Uganda->connections[0] = Rwanda; 
		Uganda->connections[1] = Sudan; 
		Uganda->connections[2] = DRC;
		Uganda->connections_size = 3;

		
		printf("done with the coundtries\n");


	//
	// Work on the list
		room_list = (struct room**)malloc(sizeof(void*)*13);

		room_list[0] = DRC;
		room_list[1] = Cameroon;
		room_list[2] = Angola;
		room_list[3] = Nigeria;
		room_list[4] = Tunisia;
		room_list[5] = RCA;
		room_list[6] = Ethiopia;
		room_list[7] = Chad;
		room_list[8] = Togo;
		room_list[9] = Niger;
		room_list[10] = Sudan;
		room_list[11] = Rwanda;
		room_list[12] = Uganda;
	//

	struct sockaddr_in sad;
	uint16_t port = 5141;
	if(argc > 1)
		port = atoi(argv[1]);
	sad.sin_port = htons(port);
	sad.sin_addr.s_addr = INADDR_ANY;
	sad.sin_family = AF_INET;

	skt = socket(AF_INET, SOCK_STREAM, 0);
	printf("done");
	struct sigaction handler;
	handler.sa_handler = stop_server;
	sigaction(SIGINT, &handler, 0);
	sigaction(SIGTERM, &handler, 0);
	sigaction(SIGSEGV, &handler, 0);

	struct sigaction pipe_handler;
	pipe_handler.sa_handler = pipe_fn;
	sigaction(SIGPIPE, &pipe_handler, 0);

	bind(skt, (struct sockaddr *)(&sad), sizeof(struct sockaddr_in));
	listen(skt, 5);
	int client_fd;
	struct sockaddr_in client_addr;
	client_list = (struct clinfo**)malloc(sizeof(void*)*1024);
	client_list_mem = 1024;
	for(;;){
		socklen_t client_address_length = sizeof(struct sockaddr);
		client_fd = accept(skt, (struct sockaddr*)&client_addr, &client_address_length);
		pthread_mutex_lock(&cl_mutex);
		printf("Accepted a connection from %s\n", inet_ntoa(client_addr.sin_addr));
		if(client_list_size >= client_list_mem){
			client_list = (struct clinfo**)realloc(client_list, sizeof(void*) * (client_list_mem + 1024));
			client_list_mem += 1024;
		}
		pthread_mutex_unlock(&cl_mutex);
		// Start a thread to talk to that client
		pthread_t thread;
		struct clinfo *ctp = (struct clinfo*)calloc(sizeof(struct clinfo), 1);
		ctp->skt = client_fd;
		
		
		strncpy(ctp->ipaddr, inet_ntoa(client_addr.sin_addr), 16);
		pthread_create(&thread, 0, client_thread, ctp);
	}
	//free countries
		close(skt);
		free(DRC);
		free(Cameroon);
		free(Chad);
		free(Angola);
		free(Togo);
		free(Niger);
		free(Nigeria);
		free(Ethiopia);
		free(Rwanda);
		free(Uganda);
		free(Tunisia);
		
	//free others
		free(client_list);

	//free monsters
		free(Kabila);
		free(Biya);
		free(Lourenco);
		free(Buhari);
		free(Saied);
		free(Ahmad);
		free(Zewde);
		free(Deby);
		free(Gnassingbe);
		free(Issoufou);
		free(alBashir);
		free(Kagame);
		free(Museveni);
		free(Crocodile);
		free(lionhead);


	return 0;
}
