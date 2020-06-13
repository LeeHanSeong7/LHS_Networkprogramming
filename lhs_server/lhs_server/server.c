#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include <winsock2.h>
#include <string.h>
#include <windows.h>

#define BUF_SIZE 1024
#define NAME_SIZE 30
#define DEF_NAME "2016115743 ���Ѽ�"
#define DEF_PORT 50000
#define MAX_CLI 20
#define MAX_ROOM 11
#define READ	3
#define	WRITE	5

#define ST_WAIT 0
#define ST_CHAT 1

//state ����//
#define S_EMPTY -1
#define S_WAIT	0
#define S_RECV	1
#define S_SEND	2
#define S_CHAT	3
//----------//

//
int g_max_client = MAX_CLI;
int g_port = 0;
//

typedef struct user_data {
	// unique
	char name[NAME_SIZE];
	SOCKET sock;

	//non_unique
	int state; 
	int room; // -1�� ����, �������϶��� ��û���� room�� ǥ��
} USR;

typedef struct    // socket info
{
	SOCKET hClntSock;
	SOCKADDR_IN clntAdr;
} PER_HANDLE_DATA, * LPPER_HANDLE_DATA;

typedef struct    // buffer info
{
	OVERLAPPED overlapped;	//���� �ʿ�
	WSABUF wsaBuf;			//���� �ʿ�
	char buffer[BUF_SIZE];
	int rwMode;    // READ or WRITE
} PER_IO_DATA, * LPPER_IO_DATA;

DWORD WINAPI EchoThreadMain(LPVOID CompletionPortIO);
void ErrorHandling(char* message);

int u_index(void* data, int target);
int u_in(USR usr);
int u_out(USR usr); 
int* u_state_filter(int state, int room);
int opr_check(char* buf);
int r_open();

int user_num = 0;
USR user_list[MAX_CLI];
int room_list[MAX_ROOM]; // ������ ����, 0�̸� ���� ��

int main(int argc, char* argv[]) {
	WSADATA wsaData;
	SOCKET hServSock;
	SOCKADDR_IN servAdr;
	LPPER_IO_DATA ioInfo;
	LPPER_HANDLE_DATA handleInfo;

	HANDLE hComPort;
	SYSTEM_INFO sysInfo;

	int recvBytes, i, flags = 0;
	char str[BUF_SIZE];

	//	init	//
	if (argc != 2) {
		printf("Type port, if you want default port <%d>, type 'Y'or'y' : ", DEF_PORT);

		scanf("%s",str);
		i = strcmp(str,"Y");

		if (strcmp(str, "Y") == 0 || strcmp(str, "y") == 0) {
			printf("server port : %d", DEF_PORT);
			g_port = DEF_PORT;
		}
		else
			g_port = atoi(str);
	}

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		ErrorHandling("WSAStartup() error!");

	for (i = 0; i < MAX_CLI; i++)
		user_list[i].state = -1;
	for (i = 0; i < MAX_ROOM; i++)
		room_list[i] = 0;
	//----------//

	//	server socket add	//
	hServSock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	memset(&servAdr, 0, sizeof(servAdr));
	servAdr.sin_family = AF_INET;
	servAdr.sin_addr.s_addr = htonl(INADDR_ANY);
	servAdr.sin_port = htons(g_port);

	bind(hServSock, (SOCKADDR*)&servAdr, sizeof(servAdr));
	listen(hServSock, MAX_CLI);
	//----------------------//

	//	CompletionPort, thread pool create	//
	hComPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	GetSystemInfo(&sysInfo);
	for (i = 0; i < sysInfo.dwNumberOfProcessors; i++) {
		if (_beginthreadex(NULL, 0, EchoThreadMain, (LPVOID)hComPort, 0, NULL) == 0)
			ErrorHandling("_beginthreadex() error!");
	}
	//--------------------------------------//

	//	body	//
	while (1) {
		SOCKET hClntSock;
		SOCKADDR_IN clntAdr;
		int addrLen = sizeof(clntAdr);

		hClntSock = accept(hServSock, (SOCKADDR*)&clntAdr, &addrLen);// accept ���

		//������ ������ �Ѱ���
		handleInfo = (LPPER_HANDLE_DATA)malloc(sizeof(PER_HANDLE_DATA));
		handleInfo->hClntSock = hClntSock;
		memcpy(&(handleInfo->clntAdr), &clntAdr, addrLen);
		//user�� ���

		CreateIoCompletionPort((HANDLE)hClntSock, hComPort, (DWORD)handleInfo, 0);
		// hClntSock�� hComPort ����,handleInfo�� ���� ���� �Ѱ���

		//	�Ѱ��� ������ �ۼ�	//
		ioInfo = (LPPER_IO_DATA)malloc(sizeof(PER_IO_DATA));
		memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
		ioInfo->wsaBuf.len = BUF_SIZE;		//wsaBuf�� ���̿� BUF_SIZE
		ioInfo->wsaBuf.buf = ioInfo->buffer;//wsabuf�� ���ۿ� ����ü�� ���� ����
		ioInfo->rwMode = READ;				//ó�� ������ �б�� ����
		//----------------------//

		WSARecv(handleInfo->hClntSock, &(ioInfo->wsaBuf),
			1, &recvBytes, &flags, &(ioInfo->overlapped), NULL);
	}
	//----------//

	//	term	//
	WSACleanup();
	return 0;
	//----------//
}

DWORD WINAPI EchoThreadMain(LPVOID pComPort){
	HANDLE hComPort = (HANDLE)pComPort;
	SOCKET sock;
	DWORD bytesTrans;
	LPPER_HANDLE_DATA handleInfo; // Ŭ���̾�Ʈ�� ���ϰ� �ּ� ����
	LPPER_IO_DATA ioInfo; // Ŭ���̾�Ʈ �������� ����
	DWORD flags = 0;
	USR user, target;

	int u_i,i,j,k;
	char buf[BUF_SIZE];
	char str[BUF_SIZE];
	char name[NAME_SIZE];
	int* filter_arr;

	while (1) {
		//�̺�Ʈ �߻��Ѱ��� cp���� ���� ���ڵ鿡 ����
		GetQueuedCompletionStatus(hComPort, &bytesTrans, //complition port�� �Ѱ���, �ۼ��� ������ ũ������
			(LPDWORD)&handleInfo, (LPOVERLAPPED*)&ioInfo, INFINITE); //hComPort������  handleInfo, ioInfo�� ����,���ð� ����
		sock = handleInfo->hClntSock; // sock�� ��� Ŭ���̾�Ʈ ���� ���

		//	body	//
		if (ioInfo->rwMode == READ) {
			puts("message received!");
			if (bytesTrans == 0)    // EOF ���� ��
			{
				closesocket(sock);
				free(handleInfo); free(ioInfo);
				continue;
			}
			//������ ���� ����//
			u_i = u_index(&sock, 2);
			if (u_i == -1) {	//�ű�����
				strcpy(user.name, DEF_NAME);
				user.sock = sock;
				user.state = S_WAIT;
				user.room = -1;

				u_i = u_in(user);	//����Ʈ�� ���� �߰�
				if (u_i == -1) {}	// ��������Ʈ�� ������ ����ó��
				else {}
			}
			else				//��������
				user = user_list[u_i];
			//----------------//

			// ���� �޽��� //
			strcpy(buf, &(ioInfo->wsaBuf));

			i = opr_check(buf);
				switch (i) {	//��ɾ� üũ
				case -1:	//�Ϲ� ä��
					break;
				case 1:		//����
					if (user.state == S_WAIT) {
						user_list[u_i].state = S_EMPTY;

						strcpy(str, "���� ����.\n");

						// �۽� //
						memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
						ioInfo->wsaBuf.buf = str;
						ioInfo->wsaBuf.len = strlen(str);
						ioInfo->rwMode = WRITE;
						WSASend(sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
						//------//
						closesocket(sock);
						free(handleInfo); free(ioInfo);
						continue;

						printf("[%s] ���� ����\n", user.name);
					}
					else
						sprintf(str, "���ǿ��� ���� ����, ä�����̶�� !E, !e�� ä�� ����.\n");

					break;

				case 2:		//����ڸ���Ʈ
					if (user.state == S_WAIT) {	// ����
						strcpy(str, "");
						for (i = 0; i < MAX_CLI; i++) {
							j = user_list[i].state;
							switch (j) {
							case S_WAIT:
								sprintf(str, "%s�̸� : [%s] , ���� : ����\n", str, user_list[i].name);
								break;
							case S_RECV:
								sprintf(str, "%s�̸� : [%s] , ���� : ä�ÿ�û����\n", str, user_list[i].name);
								break;
							case S_SEND:
								sprintf(str, "%s�̸� : [%s] , ���� : ä�ÿ䫊�۽�\n", str, user_list[i].name);
								break;
							case S_CHAT:
								sprintf(str, "%s�̸� : [%s] , ���� : ä����(ch:%d)\n", str, user_list[i].name, user_list[i].room);
								break;
							default:
								break;
							}
						}
						printf("list to [%s]\n", user.name);
					}
					else if (user.state == S_CHAT) {	// ä����
						j = 1;
						for (i = 0; i < MAX_CLI; i++) {
							strcpy(str, "\t< ch.%d ������ ����Ʈ >\n");
							if (user_list[i].room == user.room)
								sprintf(str, "%s%d. [%s]",str, j++, user_list[i].name);
						}
						printf("list to [%s]\n", user.name);
					}
					else
						sprintf(str, "����, ä�����϶� ��� ����.\n");

					// �۽� //
					memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
					ioInfo->wsaBuf.buf = str;
					ioInfo->wsaBuf.len = strlen(str);
					ioInfo->rwMode = WRITE;
					WSASend(sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
					//------//
					break;

				case 3:		//ä�ù� ����
					if (user.state == S_WAIT) {
						i = r_open();
						user.state = S_CHAT;
						user.room = i;
						user_list[u_i] = user;

						sprintf(str, "ch.%d ä�ù� ����!\n",i);
						printf("[%s] open ch.%d\n", user.name,i);
					}
					else
						sprintf(str, "���ǿ��� ��� ����.\n");

					// �۽� //
					memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
					ioInfo->wsaBuf.buf = str;
					ioInfo->wsaBuf.len = strlen(str);
					ioInfo->rwMode = WRITE;
					WSASend(sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
					//------//
					break;

				case 4:		//ä�ÿ�û ����
					if (user.state == S_RECV) {
						user_list[u_i].state == S_CHAT;
						room_list[user.room]++;
						//���� �ο��鿡�� �˸�
						sprintf(str, "[%s]�� �����߽��ϴ�.\n", user.name);
						filter_arr = u_state_filter(S_CHAT, user.room);
						for (i = 0; i < MAX_CLI + 1; i++) {
							j = filter_arr[i];
							if (j == -1)
								break;
							memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
							ioInfo->wsaBuf.buf = str;
							ioInfo->wsaBuf.len = strlen(str);
							ioInfo->rwMode = WRITE;
							WSASend(user_list[j].sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
						}
					}
					else
						sprintf(str, "�ʴ븦 �޾������� ��� ����.\n");
					break;

				case 5:		//ä�ÿ�û ����
					if (user.state == S_RECV) {
						user_list[u_i].state == S_WAIT;
						//���� �ο��鿡�� �˸�
						sprintf(str, "[%s]�� �ʴ� ����.\n",user.name);
						filter_arr = u_state_filter(S_CHAT, user.room);
						for (i = 0; i < MAX_CLI + 1; i++) {
							j = filter_arr[i];
							if (j == -1)
								break;
							memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
							ioInfo->wsaBuf.buf = str;
							ioInfo->wsaBuf.len = strlen(str);
							ioInfo->rwMode = WRITE;
							WSASend(user_list[j].sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
						}
						user_list[u_i].room == -1;
					}
					else
						sprintf(str, "�ʴ븦 �޾������� ��� ����.\n");
					break;

				case 6:		//ä�� ����
					if (user.state == S_CHAT) {
						user.state = S_WAIT;
						room_list[user.room]--;
						user.room = -1;
						user_list[u_i] = user;
						sprintf(str, "ch.%d ä�ù濡�� ����!\n", i);
						printf("[%s] exit from ch.%d\n", user.name, i);
					}
					else
						sprintf(str, "���ǿ��� ��� ����.\n");

					// �۽� //
					memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
					ioInfo->wsaBuf.buf = str;
					ioInfo->wsaBuf.len = strlen(str);
					ioInfo->rwMode = WRITE;
					WSASend(sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
					//------//
					break;

				case 7:		//�̸� ����
					strcpy(str, user.name);
					sscanf(buf, "!1%s\n", name);
					i = u_index(name, 1);
					if (i == -1) {
						strcpy(user.name, name);
						strcpy(user_list[u_i].name, name);
						sprintf(str, "[%s]���� [%s]�� �̸� ����\n", str, name);

						puts(str);
					}
					else
						sprintf(str, "!1\n", name);

					// �۽� //
					memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
					ioInfo->wsaBuf.buf = str;
					ioInfo->wsaBuf.len = strlen(str);
					ioInfo->rwMode = WRITE;
					WSASend(sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
					//------//
					break;

				case 8:		//ä�ù� ����Ʈ
					strcpy(str, "");
					for (i = 0; i < MAX_ROOM; i++) {
						if (room_list[i] != 0) {
							sprintf(str, "%s <ch.%d>\n", str, i);
							filter_arr = u_state_filter(S_CHAT, i);
							k = 1;
							for (j = 0; j < MAX_CLI + 1; j++) {
								if (filter_arr[j] == -1)
									break;
								sprintf(str, "%s%d) [%s]\n", str, k++, user_list[filter_arr[j]].name);
							}
						}
					}
					// �۽� //
					memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
					ioInfo->wsaBuf.buf = str;
					ioInfo->wsaBuf.len = strlen(str);
					ioInfo->rwMode = WRITE;
					WSASend(sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
					//------//
					break;

				case 9:		//ä�� �ʴ�
					if (user.state == S_CHAT)
						sprintf(str, "!2", i);
					else
						sprintf(str, "���ǿ��� ��� ����.\n");

					// �۽� //
					memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
					ioInfo->wsaBuf.buf = str;
					ioInfo->wsaBuf.len = strlen(str);
					ioInfo->rwMode = WRITE;
					WSASend(sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
					//------//
					break;

				case 10:	//���� ���� ���
					i = user.state;
					switch (i) {
					case S_WAIT:
						sprintf(str, "�̸� : [%s] , ���� : ����\n", user.name);
						break;
					case S_RECV:
						sprintf(str, "�̸� : [%s] , ���� : ä�ÿ�û����\n", user.name);
						break;
					case S_SEND:
						sprintf(str, "�̸� : [%s] , ���� : ä�ÿ䫊�۽�\n", user.name);
						break;
					case S_CHAT:
						sprintf(str, "�̸� : [%s] , ���� : ä����(ch:%d)\n", user.name, user.room);
						break;
					default:
						break;
					}
					break;

				case 11:	//�ʴ� ��� �̸� ȹ��, �ʴ� ����
					sscanf(buf, "!2%s\n", name);
					i = u_index(name, 1);
					if (i == -1)
						sprintf(str, "����� ����, ����Ʈ Ȯ�� : !L or !l\n");
					else {
						if (user_list[i].state == S_WAIT) {
							user_list[i].state == S_RECV;
							user_list[i].room == user.room;

							sprintf(str, "!3[%s]�� ä�ù� ch.%d�� �ʴ��߽��ϴ�. (����: !Y,!y)(����: !N,!n)\n", name, user.room);
							// �۽� //
							memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
							ioInfo->wsaBuf.buf = str;
							ioInfo->wsaBuf.len = strlen(str);
							ioInfo->rwMode = WRITE;
							WSASend(user_list[i].sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
							//------//

							sprintf(str, "[%s]�� ch.%d �� �ʴ��߽��ϴ�.\n",name,user.room);
						}
						else {
							if (target.state == S_RECV)
								sprintf(str, "����� ���� �ʴ����\n");
							else if (target.state == S_CHAT)
								sprintf(str, "����� ä����\n");
							else
								sprintf(str, "����� ���°� Ȯ�� �ȵ�\n");
						}
					}

					// �۽� //
					memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
					ioInfo->wsaBuf.buf = str;
					ioInfo->wsaBuf.len = strlen(str);
					ioInfo->rwMode = WRITE;
					WSASend(sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
					//------//
					break;
				}
			//-------------//

			//	������ ���� ��û	//
			ioInfo = (LPPER_IO_DATA)malloc(sizeof(PER_IO_DATA));
			memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
			ioInfo->wsaBuf.len = BUF_SIZE;
			ioInfo->wsaBuf.buf = ioInfo->buffer;
			ioInfo->rwMode = READ;
			WSARecv(sock, &(ioInfo->wsaBuf),
				1, NULL, &flags, &(ioInfo->overlapped), NULL);
			//----------------------//
		}
		else {
			puts("message sent!");
			free(ioInfo);
		}
	}
}
void ErrorHandling(char* message){
	fputs(message, stderr);
	fputc('\n', stderr);
	exit(1);
}
/*
	target ���� 
		name : 1
		sock : 2
*/
int u_index(void* data, int target) {
	int i;
	switch (target) {
		case 1:		//name
			for (i = 0; i < user_num; i++) {
				if (strcmp(user_list[i].name, (char*)data) == 0)
					return i;
			}
			return -1; // ��ġ�Ǵ°� ����
		case 2:		//sock
			for (i = 0; i < user_num; i++) {
				if (user_list[i].sock == (SOCKET*)data)
					return i;
			}	
			return -1; // ��ġ�Ǵ°� ����
		default :
			return -2; // Ÿ�پ���
	}
}

int u_in(USR usr) {
	int i;

	for (i = 0; i < user_num; i++) {
		if (user_list[i].state == S_EMPTY) {
			user_list[i] = usr;
			return i;
		}
	}
	return -1;
}
int u_out(USR usr) {
	int i;

	for (i = 0; i < user_num; i++) {
		if (user_list[i].sock == usr.sock) {
			user_list[i].state = S_EMPTY;
			return i;
		}
	}
	return -1;
}

// ���� üũ, room�� -1�� ������ ��� ����, return�� �ε��� �迭�� �ָ� ���� -1�� �� ����
int* u_state_filter(int state, int room) { 
	int i,num=0;
	int arr[MAX_CLI+1];

	arr[0] = -1;
	if (room == -1) {
		for (i = 0; i < user_num; i++)
			if (user_list[i].state == state)
				arr[num++] = i;
	}
	else {
		for (i = 0; i < user_num; i++)
			if (user_list[i].state == state && user_list[i].room == room)
				arr[num++] = i;
	}
	arr[num + 1] = -1;
	return arr;
}

/*
	����					:	!Q or !q	1	//���ǿ��� ����
	����� ����Ʈ			:	!L or !l	2	//���ǿ����� ��ü ����Ʈ, ä�����϶��� ���� ����Ʈ.
	ä�ù� ���� 			:	!R or !r	3	//���ǿ��� ����
	ä�� ��û ����			:	!Y or !y	4	//��û ���� �����϶��� ����
	ä�� ��û ����			:	!N or !n	5	//��û ���� �����϶��� ����
	ä�� ����				:	!E or !e	6	//ä���� ���¿��� ����
	�̸�����				:	!1name		7	//Ŭ�󿡼� �۾��� ���� , �̸���ġ�� Ŭ��� ��ɾ�(!1)
	ä�� ����Ʈ ���		:	!C or !c	8	//���ǿ��� ����
	ä�� �ʴ�				:	!I or !i	9	//ä�ù濡�� ����	Ŭ���̾�Ʈ���� ���(!3!text)
	���� ���� ���			:	!S or !s	10	//���� ���, ���� ��Ȳ ���
	�ʴ� ��� ȹ��			:	!2name!room		11	//�ʴ��û�� ���� �̸��� ȹ��. �̰��� �����°��� �����ʿ��� Ŭ�������� ��ɾ�(!2!room)�� ������, �ʴ�� ������� ��븸 ����
	-1�� ����� �ƴѰɷ� �Ǵ� , �Ϲ� ��ȭ�� ��� (!0)
*/
int opr_check(char* buf) {
	if (buf[0] != '!') return -1;

	if (strcmp(buf, "!Q") == 0 || strcmp(buf, "!q") == 0)
		return 1;
	else if (strcmp(buf, "!L") == 0 || strcmp(buf, "!l") == 0)
		return 2;
	else if (strcmp(buf, "!R") == 0 || strcmp(buf, "!r") == 0)
		return 3;
	else if (strcmp(buf, "!Y") == 0 || strcmp(buf, "!y") == 0)
		return 4;
	else if (strcmp(buf, "!N") == 0 || strcmp(buf, "!n") == 0)
		return 5;
	else if (strcmp(buf, "!E") == 0 || strcmp(buf, "!e") == 0)
		return 6;
	else if (buf[1] == '1')	// !exist
		return 7;
	else if (strcmp(buf, "!C") == 0 || strcmp(buf, "!c") == 0)
		return 8;
	else if (strcmp(buf, "!I") == 0 || strcmp(buf, "!i") == 0)
		return 9;
	else if (strcmp(buf, "!S") == 0 || strcmp(buf, "!s") == 0)
		return 10;
	else if (buf[1] == '2')	// !target!room
		return 11;
	else
		return -1;
}
int r_open() {
	int i;

	for(i=0;i< MAX_ROOM;i++)
		if (room_list[i] == 0) {
			room_list[i] == 1;
			return i;
		}
	return -1;
}