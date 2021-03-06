#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cnaiapi.h>
#include <winsock2.h>

#define N_BUFFSIZE 4
#define BUFFSIZE 256

#ifdef _MSC_VER
	#pragma warning(disable:4996)
	// #pragma comment(lib, "Ws2_32.lib")
#endif

enum {
	PLAYER_ASSIGN = 10,
	PLAYER_WAIT_MESSAGE = 11,
	SERVER_SENT_BOARDS = 12,

	PLAYER_TURN_CHANGED = 20,
	SERVER_REQUESTED_MOVE = 21,
	PLAYER_MOVE_VALID = 22,
	PLAYER_MOVE_INVALID = 23,
	PLAYER_MOVE_INVALID_REASON = 24,
	PLAYER_MOVE_INFO_MESSAGE = 25,
	GAME_FINISHED = 26,

	PLAYER_DISCONNECTED = 30,
};

void make_conn(computer *comp, connection *conn);
void disconnected();

void send_one(char buff[], connection conn);
int recv_action(connection conn);
int recv_one(char buff[], connection conn);

int main()
{
	computer comp;
	connection conn;
	char buff[BUFFSIZE];
	char playerboards[BUFFSIZE];
	int len, action;
	int done = 0;
	int player, turn, attempts;

	/* Attempt connection via Navy.conf */
	make_conn(&comp, &conn);

	/* Initialize buffer */
	memset(playerboards, 0, BUFFSIZE);

	/* Loop until game is finished or the opponent has disconnected */
	while ((action = recv_action(conn)) != GAME_FINISHED && action != PLAYER_DISCONNECTED)
	{
		switch (action)
		{
			case PLAYER_ASSIGN:
			{
				/* Assign player number */
				recv_one(buff, conn);
				player = atoi(buff);
				printf("Assigned as player %d.\n", player + 1);
				fflush(stdout);
				break;
			}
			case PLAYER_WAIT_MESSAGE:
			{
				/* Receive message waiting for player 2 */
				len = recv_one(buff, conn);
				write(STDOUT_FILENO, buff, len);
				break;
			}
			case SERVER_SENT_BOARDS:
			{
				/* Print the boards */
				len = recv_one(buff, conn);
				write(STDOUT_FILENO, buff, len);

				/* Store buffer locally*/
				strncpy(playerboards, buff, len);
				break;
			}
			case PLAYER_TURN_CHANGED:
			{
				/* A new turn has started */
				attempts = 3;

				/* Check if it's our turn*/
				len = recv_one(buff, conn);
				turn = atoi(buff);

				if (player == turn)
				{
					printf("It's your turn!\n");
					fflush(stdout);
				}
				else
				{
					printf("It's your opponent's turn!\n");
					fflush(stdout);
				}
				break;
			}
			case SERVER_REQUESTED_MOVE:
			{
				/* We need to send a X,Y move */
				printf("Enter X,Y value: ");
				fflush(stdout);
				scanf("%s", buff);
				send_one(buff, conn);
				break;
			}
			case PLAYER_MOVE_VALID:
				/* Useless but mostly for reference */
				break;
			case PLAYER_MOVE_INVALID:
			{
				/* Move is invalid, provide info to enemy */
				attempts--;
				if (!player == turn)
				{
					printf("Your opponent has made an incorrect move.\n");
					fflush(stdout);

					/* Reload board locally */
					printf("%s\n", playerboards);
					fflush(stdout);

					if (attempts > 0)
						printf("Waiting for another move...\n\n");						
					else
						printf("Your opponent is out of moves!\n\n");
					fflush(stdout);
				}
				break;
			}
			case PLAYER_MOVE_INVALID_REASON:
			{
				/* Move is invalid, provide DETAILED info to current player */
				len = recv_one(buff, conn);
				write(STDOUT_FILENO, buff, len);

				/* Reload board locally */
				printf("%s\n", playerboards);
				fflush(stdout);

				if (!attempts)
					printf("You are out of moves!\n\n");
				break;
			}
			case PLAYER_MOVE_INFO_MESSAGE:
			{
				/* Provide enemy the move current player made */
				len = recv_one(buff, conn);
				write(STDOUT_FILENO, buff, len);
				break;
			}
		}
	}

	if (action == PLAYER_DISCONNECTED)
	{
		printf("Your opponent disconnected.\n");
		printf("You win!\n");
	}
	else if (action == GAME_FINISHED)
	{
		if (player == turn)
			printf("You won!\n");
		else
			printf("You lost.\n");
	}

	printf("Game ended.\n");
	fflush(stdout);

	system("pause");
	return 0;
}

void make_conn(computer *comp, connection *conn)
{
	FILE *fp;
	char ip[16];
	char termch;
	int nscan;
	appnum port;

	/* Open file */
	while (1) 
	{
		printf("Reading Navy.conf\n");
		fflush(stdout);

		fp = fopen("Navy.conf", "r");
		if (fp != NULL)
			break;

		printf("Failed to open Navy.conf\n");
		fflush(stdout);
		system("pause");
		exit(0);
	}

	/* Read the first line which contains the IP address */
	nscan = fscanf(fp, "%s%c", ip, &termch);

	if (nscan != 2 || termch != '\n')
	{
		printf("Improper file format.\n");
		fflush(stdout);
		system("pause");
		exit(0);
	}

	/* Convert to comp, fail if invalid */
	*comp = cname_to_comp(ip);
	if (*comp < 0)
	{
		printf("Invalid IP address.\n");
		fflush(stdout);
		system("pause");
		exit(0);
	}

	/* Read the second line which contains the port number */
	nscan = fscanf(fp, "%hd", &port);

	if (nscan != 1)
	{
		printf("Improper file format.\n");
		fflush(stdout);
		system("pause");
		exit(0);
	}

	/* Attempt gameserver connection, exit if failed */
	*conn = make_contact(*comp, port);
	
	if (*conn < 0)
	{
		printf("Could not connect to gameserver.\n");
		fflush(stdout);
		system("pause");
		exit(0);
	}
}

void disconnected()
{
	printf("Disconnected from server.\n");
	system("pause");
	exit(0);
}

/* Note that the functions below break if:
 * 1) Int size is different between platforms (2 vs 4 bytes)
 *
 * However, since the program will be supposedly compiled with
 * the same compiler and used by PCs within the last two
 * decades, this should not be an issue.
 */

void send_one(char buff[], connection conn)
{
	/* Sends a length field, then the message. This means that 
	   we will not have consolidated messages in one recv. */

	int len = strlen(buff);

	/* Account for endianess */
	int c_len = htonl(len);

	/* Send message length */
	send(conn, &c_len, sizeof(c_len), 0);

	/* Send message */
	send(conn, buff, len, 0);
}

int recv_action(connection conn)
{
	/* Receives a game opcode from the server */

	int c_action, action, response;
	
	/* Receive the message */
	response = recv(conn, &c_action, sizeof(c_action), 0);

	/* If response < 1, server disconnected */
	if (response < 1)
		disconnected();

	/* Account for endianess */
	action = ntohl(c_action);

	/* Return game opcode */
	return action;
}

int recv_one(char buff[], connection conn)
{
	/* Receives a length field, then the message from the server */

	int c_len, len, response;

	/* Receive message length */
	response = recv(conn, &c_len, sizeof(c_len), 0);

	/* If response < 1, client disconnected */
	if (response < 1)
		disconnected();

	/* Account for endianess */
	len = ntohl(c_len);

	/* Receive the message */
	recv(conn, buff, len, 0);

	/* Return message length */
	return len;
}
