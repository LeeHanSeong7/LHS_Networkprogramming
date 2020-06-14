// KNU 2016115743 ���Ѽ�
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include <winsock2.h>
#include <string.h>
#include <windows.h>

#define BUF_SIZE 1024
#define NAME_SIZE 30
#define DEF_NAME	"2016115743 ���Ѽ�"
#define DEF_SVRIP	"127.0.0.1"
#define DEF_PORT 50000

//	TEXT	//
#define T_EMPTY	""
#define T_WAIT	"<���� ��ɾ�> ����:!Q, ä�ù� ����:!R, ����ڸ���Ʈ:!L ä�ø���Ʈ:!C, ������Ȯ��:!S ..:"
#define T_RECV	"����:!Y, ����:!N ..:"
#define T_SEND	""
#define T_CHAT	"<ä�ù� ��ɾ�> ä������:!E, ����ڸ���Ʈ:!L, ������Ȯ��:!S, ä���ʴ�:!I ..:"
//----------//

//	�������� ���	//
#define SC_CHAT			"!0"
#define SC_EXIST		"!1"
#define	SC_ITARGET		"!2"
#define	SC_INVITE		"!3!"
#define SC_SINFO		"!6!"
//------------------//

//--------------//
/*	Ŭ���̾�Ʈ���� ���//
	����					:	!Q or !q				1	//���ǿ��� ����
	����� ����Ʈ			:	!L or !l				2	//���ǿ����� ��ü ����Ʈ, ä�����϶��� ���� ����Ʈ.
	ä�ù� ���� 			:	!R or !r				3	//���ǿ��� ����
	ä�� ��û ����			:	!Y or !y				4	//��û ���� �����϶��� ����
	ä�� ��û ����			:	!N or !n				5	//��û ���� �����϶��� ����
	ä�� ����				:	!E or !e				6	//ä���� ���¿��� ����
	�̸�����				:	!CC_NAMEname			7	//Ŭ�󿡼� �۾��� ���� , �̸���ġ�� Ŭ��� ��ɾ�(SC_EXIST)*/
#define	CC_NAME	"!1"
/*	ä�� ����Ʈ ���		:	!C or !c				8	//���ǿ��� ����
	ä�� �ʴ�				:	!I or !i				9	//ä�ù濡�� ����	Ŭ���̾�Ʈ���� ���(SC_ITARGET)
	���� ���� ���			:	!S or !s				10	//���� ���, ���� ��Ȳ ���
	�ʴ� ��� ȹ��			:	!CC_ITARGETname			11	//�ʴ��û�� ���� �̸��� ȹ��.ä�ÿ�û�� ���� ����, �ʴ�� ������� ��븸 ����*/
#define	CC_ITARGET	"!2"	
//	-1�� ����� �ƴѰɷ� �Ǵ� , �Ϲ� ��ȭ�� ��� (!CC_CHAT)*/
#define	CC_CHAT	"!0"
//----------------------//

//state ����//
#define S_EMPTY -1
#define S_WAIT	0
#define S_RECV	1
#define S_SEND	2
#define S_CHAT	3
//----------//

////
int g_port;
char g_servip[17];
char userName[NAME_SIZE];
CRITICAL_SECTION g_CS;
////

void ErrorHandling(char* message);
int opr_check(char* buf);
int ser_opr_check(char* buf);
int print_info(int state);
DWORD WINAPI recvThread(SOCKET* sock);

int main() {
	char user_buf[BUF_SIZE];
	char* buf;
	char str[BUF_SIZE];
	char str2[BUF_SIZE];
	WSADATA wsaData;
	SOCKET servSock;
	SOCKADDR_IN servAdr;

	int opr, i, j, k;
	int recvBytes,sendBytes, flags;
	//	init	//
	printf("Type IP what you want to connect, if you want default IP <%s>, type 'Y'or'y' : ", DEF_SVRIP);
	scanf("%s", str);

	if (strcmp(str, "Y") == 0 || strcmp(str, "y") == 0) {
		strcpy(g_servip, DEF_SVRIP);
		printf("server ip : %s\n", DEF_SVRIP);
	}
	else {
		sscanf(str,"%s\n", g_servip);
		printf("server ip : %d\n", g_servip);
	}
	printf("Type port what you want to connect, if you want default port <%d>, type 'Y'or'y' : ", DEF_PORT);
	scanf("%s", str);

	if (strcmp(str, "Y") == 0 || strcmp(str, "y") == 0) {
		g_port = DEF_PORT;
		printf("server port : %d\n", DEF_PORT);
	}
	else {
		g_port = atoi(str);
		printf("server port : %d\n", g_port);
	}

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		ErrorHandling("WSAStartup() error!");
	//----------//

	//	server socket add	//
	servSock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	memset(&servAdr, 0, sizeof(servAdr));
	servAdr.sin_family = AF_INET;
	servAdr.sin_addr.s_addr = inet_addr(g_servip);
	servAdr.sin_port = htons(g_port);
	//----------------------//

	//	connect	//
	if (connect(servSock, (struct sockaddr*) & servAdr, sizeof(servAdr)) == SOCKET_ERROR)
		ErrorHandling("connect() error!");
	else
		printf("Welcome to Chat server!\n");
	//----------//

	//	body	//
	buf = (char*)malloc(sizeof(char)*BUF_SIZE);
		//�̸� ������ ����//
		while (1) {
			printf("type your name : ");
			scanf("%s", str2);
			sprintf(str, "%s%s", CC_NAME, str2);
			sendBytes = send(servSock, str, BUF_SIZE, 0);
			if (sendBytes == 0)
				ErrorHandling("error at naming request");
			else {
				if (recv(servSock, user_buf, BUF_SIZE, 0) == 0)
					ErrorHandling("error at naming request");
				if (strcmp(user_buf, SC_EXIST) == 0) {
					printf("�̸��� �̹� ������Դϴ�.");
					continue;
				}
				else {
					buf = strtok(user_buf, CC_CHAT);
					puts(buf);
					strcpy(userName, str2);
					break;
				}
			}
		}
		print_info(S_WAIT);
		//----------------//
		//	thread	//
		InitializeCriticalSection(&g_CS);
		_beginthreadex(NULL, 0, recvThread, (SOCKET*)(&servSock), 0, NULL);
		//----------//

		//	send	//
		while (1) {
			scanf("%s", user_buf);
			opr = opr_check(user_buf);
			if (opr == 0) {//�Ϲ� ä��
				sprintf(str2, "%s%%s", CC_CHAT);
				sprintf(str,str2,user_buf);
			}
			else {
				strcpy(str, user_buf);
			}
			sendBytes = send(servSock, str, BUF_SIZE, 0);
			if (sendBytes == 0) printf("send fail.\n");

			/*EnterCriticalSection(&g_CS);
			printf("[%s]:", userName);
			LeaveCriticalSection(&g_CS);*/
		}
		//----------//
	//----------//
	DeleteCriticalSection(&g_CS);
	closesocket(servSock);
	WSACleanup();
	free(buf);
	return 0;
}
void ErrorHandling(char* message) {
	fputs(message, stderr);
	fputc('\n', stderr);
	exit(1);
}
int opr_check(char* buf) {
	if (buf[0] != '!') return 0;

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
	else if (strcmp(buf, "!C") == 0 || strcmp(buf, "!c") == 0)
		return 8;
	else if (strcmp(buf, "!I") == 0 || strcmp(buf, "!i") == 0)
		return 9;
	else if (strcmp(buf, "!S") == 0 || strcmp(buf, "!s") == 0)
		return 10;
	else
		return 0;
}
int ser_opr_check(char* buf) {
	if (buf[0] != '!') return -1;

	if (buf[1] == SC_CHAT[1])
		return 0;
	else if (buf[1] == SC_EXIST[1])
		return 1;
	else if (buf[1] == SC_ITARGET[1])
		return 2;
	else if (buf[1] == SC_INVITE[1])
		return 3;
	else if (buf[1] == SC_SINFO[1])
		return 6;

	return -1;
}
int print_info(int state){
	switch (state) {
	case S_EMPTY:
		printf(T_EMPTY);
		break;
	case S_WAIT:
		printf(T_WAIT);
		break;
	case S_RECV:
		printf(T_RECV);
		break;
	case S_SEND:
		printf(T_SEND);
		break;
	case S_CHAT:
		printf(T_CHAT);
		break;
	default:
		printf("���� ����\n");
		break;
	}
	return 0;
}
DWORD WINAPI recvThread(SOCKET* sock) {
	char serv_buf[BUF_SIZE];
	char* buf;
	char str[BUF_SIZE];
	int opr, i;

	while (1) {
		if (recv(*sock, serv_buf, BUF_SIZE, 0) == 0)
			ErrorHandling("error at server");
		opr = ser_opr_check(serv_buf);
		switch (opr) {
		case 0:	//	SC_CHAT
			buf = strtok(serv_buf, SC_CHAT);
			puts(buf);
			break;
		case 2:	//	SC_ITARGET
			EnterCriticalSection(&g_CS);
			while (1) {
				printf("�ʴ��Ϸ��� ������ �̸��� �Է����ּ��� : ");
				fflush(stdin);
				scanf("%s", str);
				if (strcmp(str, userName) == 0) {
					printf("�ڽ��� �ʴ��� �� �����ϴ� : ");
					continue;
				}
				strcpy(buf, CC_ITARGET);
				strcat(buf, str);
				i = send(*sock, buf, BUF_SIZE, 0);
				if (i == 0) printf("send fail.\n");
			}
			LeaveCriticalSection(&g_CS);
			break;
		case 3:	//	SC_INVITE
			buf = strtok(serv_buf, SC_INVITE);
			puts(buf);
			fflush(stdin);
			scanf("%s", str);
			i = send(*sock, str, BUF_SIZE, 0);
			if (i == 0) printf("send fail.\n");
			break;
		case 6:
			buf = strtok(serv_buf, SC_SINFO);
			sscanf(buf,"%d",&i);
			print_info(i);
			break;
		default:
			break;
		}/*
		EnterCriticalSection(&g_CS);
		printf("[%s]:",userName);
		LeaveCriticalSection(&g_CS);*/
	}
}