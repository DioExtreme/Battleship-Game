#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <cnaiapi.h>
#include <winsock2.h>

#define N_BUFFSIZE 4
#define BUFFSIZE 256

#ifdef _MSC_VER
	#pragma warning(disable:4996)
	// #pragma comment(lib, "Ws2_32.lib")
#endif

typedef struct Navy {
	int board[5][5];
}Navy;

enum {
	// LOCAL
	SPOT_EMPTY = 0,
	SPOT_SHOT = 1,
	SPOT_SHIP_HIDDEN = 2,
	SPOT_SHIP_SHOT = 3,
	OUT_OF_BOUNDS = 4,
	ALREADY_SHOT = 5,
	MOVE_FORMAT_INVALID = 6,
	MOVE_VALID = 7,

	// REMOTE
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

	PLAYER_DISCONNECTED = 30
};

void create_boards(Navy player[]);
void print_boards(Navy player[], connection conn[]);
void game(Navy player[], connection conn[]);

int valid_format(char answer[]);
int validate_move(Navy player[], int x, int y, int enemy);

void send_action(int action, connection conn);
void send_one(char buff[], connection conn);
int recv_one(char buff[], connection conn);

int main()
{
	connection conn[2];
	char buff[BUFFSIZE];

	/* Generate random seed based on time */
	srand(time(NULL));

	/* Initialize buff */
	memset(buff, 0, BUFFSIZE);

	/* We're now listening on port 20000 */
	printf("Listening on port 20000.\n");
	fflush(stdout);

	/* Await contact from player 1 */
	conn[0] = await_contact((appnum)20000);
	printf("Player 1 connected.\n");
	printf("Sending wait message.\n");
	fflush(stdout);

	/* Tell client to assign itself as player 1 */
	send_action(PLAYER_ASSIGN, conn[0]);
	sprintf(buff, "0");
	send_one(buff, conn[0]);

	/* Send player 1 a message waiting for player 2 */
	send_action(PLAYER_WAIT_MESSAGE, conn[0]);
	sprintf(buff, "Waiting for player 2...\n");
	send_one(buff, conn[0]);

	/* Await contact from player 2 */
	conn[1] = await_contact((appnum)20000);
	printf("Player 2 connected.\n");
	fflush(stdout);

	/* Tell client to assign itself as player 2 */
	send_action(PLAYER_ASSIGN, conn[1]);
	sprintf(buff, "1");
	send_one(buff, conn[1]);

	/* Create two 5x5 boards, one for each player */
	Navy player[2] = { SPOT_EMPTY };
	create_boards(player);

	/* Print the boards to each player */
	print_boards(player, conn);

	/* Begin the game */
	game(player, conn);

	system("pause");
	return 0;
}

void create_boards(Navy player[])
{
	int i, j;
	int x, y;
	int done;

	/* Iterate twice for each player */
	for (i = 0; i < 2; i++)
	{
		for (j = 0; j < 2; j++)
		{
			done = 0;
			while (!done)
			{
				/* Generate two random numbers based on the time seed */
				x = rand() % 5;
				y = rand() % 5;

				/* Print the coordinates to the server's console */
				printf("XY for Player %d Ship:%d is <%d,%d>\n", i + 1, j + 1, x + 1, y + 1);
				fflush(stdout);
				
				/* Check if coordinates already contain a hidden ship
				   If not, assign spot, otherwise regenerate x,y */
				if (player[i].board[x][y] != SPOT_SHIP_HIDDEN)
				{
					player[i].board[x][y] = SPOT_SHIP_HIDDEN;
					done = 1;
				}
				else
				{
					printf("There is a ship in those coordinates, regenerating.\n");
					fflush(stdout);
				}
			}
		}
	}
}

void print_boards(Navy player[], connection conn[])
{
	int i, j, k, l;
	int enemy;
	char buff[BUFFSIZE];
	char n[N_BUFFSIZE];

	/* Iterate twice for each player */
	for (i = 0; i < 2; i++)
	{
		/* Initialize char arrays */
		memset(n, 0, N_BUFFSIZE);
		memset(buff, 0, BUFFSIZE);

		for (j = 0; j < 2; j++)
		{
			/* Depending on which player we're currently iterating
			   Print the correct message to each player's board */
			enemy = (i == j) ? 0 : 1;
			if (enemy)
				strcat(buff, "Your enemy's board: \n\n");
			else
				strcat(buff, "Your board: \n\n");
			strcat(buff, "  ");

			/* Print horizontal row of numbers */
			for (k = 1; k < 6; k++)
			{
				sprintf(n, " %d", k);
				strcat(buff, n);
			}
			strcat(buff, "\n");

			/* Iterate over 5x5 board */
			for (k = 0; k < 5; k++)
			{
				/* Print vertical row number */
				sprintf(n, "%d ", k + 1);
				strcat(buff, n);
				
				for (l = 0; l < 5; l++)
				{
					strcat(buff, "|");
					/* For each element print the following values: 
					 * <space> for an empty or an enemy's hidden ship spot
					 * + for a spot that has been shot
					 * S for an ally's hidden ship spot
					 * X for a spot where a ship was shot
					 */
					if (player[j].board[k][l] == SPOT_EMPTY)
						strcat(buff, " ");
					else if (player[j].board[k][l] == SPOT_SHOT)
						strcat(buff, "+");
					else if (player[j].board[k][l] == SPOT_SHIP_HIDDEN)
					{
						if (enemy)
							strcat(buff, " ");
						else
							strcat(buff, "S");
					}
					else if (player[j].board[k][l] == SPOT_SHIP_SHOT)
						strcat(buff, "X");
				}
				strcat(buff, "|\n");
			}
			strcat(buff, "\n");
		}
		/* Send boards to player */
		send_action(SERVER_SENT_BOARDS, conn[i]);
		send_one(buff, conn[i]);
	}
}

void game(Navy player[], connection conn[])
{
	char answer[BUFFSIZE], orig_answer[BUFFSIZE];
	char buff[BUFFSIZE];
	char n[N_BUFFSIZE];
	char *token;
	int len;
	int move, attempts, x, y;
	int turn = 0, enemy = 1;
	int ships_shot[2] = { 0 };

	/* turn - Indicates the player who is making a move
	   enemy - Indicates the current player's enemy */

	while (1)
	{
		/* Initialize n */
		memset(n, 0, N_BUFFSIZE);

		/* Print current turn */
		printf("It's player %d's turn.\n", turn + 1);
		fflush(stdout);

		/* Announce next turn to both players */
		send_action(PLAYER_TURN_CHANGED, conn[turn]);
		send_action(PLAYER_TURN_CHANGED, conn[enemy]);
		sprintf(n, "%d", turn);
		send_one(n, conn[turn]);
		send_one(n, conn[enemy]);

		/* We're giving 3 attempts for each turn */
		attempts = 3;
		
		/* Iterate while there are still attempts left */
		while (attempts > 0)
		{
			/* Initialize char arrays */
			memset(buff, 0, BUFFSIZE);
			memset(answer, 0, BUFFSIZE);
			memset(orig_answer, 0, BUFFSIZE);

			/* Announce to current player that we need a move */
			send_action(SERVER_REQUESTED_MOVE, conn[turn]);
			len = recv_one(buff, conn[turn]);

			/* If len < 1, current player has disconnected, enemy wins */
			if (len < 1)
			{
				send_action(PLAYER_DISCONNECTED, conn[enemy]);

				printf("Player %d has disconnected.\n", turn + 1);
				fflush(stdout);
				
				send_eof(conn[enemy]);

				system("pause");
				exit(0);
			}
			
			/* Copy buff -> answer */
			strncpy(answer, buff, len);

			/* Store the original answer so that we can 
			   announce a valid move to the enemy */
			strncpy(orig_answer, answer, len);
			printf("Player %d has made the move: %s\n", turn + 1, answer);
			fflush(stdout);

			/* Check if move has a valid format X,Y */
			if (!valid_format(answer))
			{
				/* Announce to both players that the move is invalid */
				send_action(PLAYER_MOVE_INVALID, conn[turn]);
				send_action(PLAYER_MOVE_INVALID, conn[enemy]);

				attempts--;

				printf("Invalid move format.\n");
				fflush(stdout);

				/* Announce invalid move format to current player */
				sprintf(buff, "Invalid move format! It must be X,Y\n");
				send_action(PLAYER_MOVE_INVALID_REASON, conn[turn]);
				send_one(buff, conn[turn]);
				
				/* Next attempt */
				continue;
			}

			/* Split answer and assign the coordinates to each integer */
			token = strtok(answer, ",");
			x = atoi(token) - 1;
			token = strtok(NULL, ",");
			y = atoi(token) - 1;

			/* Check if the move is valid */
			move = validate_move(player, x, y, enemy);

			if (move == MOVE_VALID)
			{
				/* Announce to both players that the move is valid */
				send_action(PLAYER_MOVE_VALID, conn[turn]);
				send_action(PLAYER_MOVE_VALID, conn[enemy]);
				break;
			}

			/* Announce to both players that the move is invalid */
			send_action(PLAYER_MOVE_INVALID, conn[turn]);
			send_action(PLAYER_MOVE_INVALID, conn[enemy]);
			attempts--;

			/* Announce the reason for the invalid move to current player */
			if (move == OUT_OF_BOUNDS)
			{
				printf("Coordinates are out of bounds.\n");
				fflush(stdout);
				sprintf(buff, "Coordinates are out of bounds!\n");		
			}
			else if (move == ALREADY_SHOT)
			{
				printf("Spot is already shot.\n");
				fflush(stdout);
				sprintf(buff, "You have already shot this spot!\n");
			}

			send_action(PLAYER_MOVE_INVALID_REASON, conn[turn]);
			send_one(buff, conn[turn]);
		}

		/* Nothing to do if the player has run out of attempts 
		   No valid move was ever played */
		if (attempts == 0)
			printf("Player %d has run out of attempts.\n", turn + 1);
		else
		{
			/* A valid move has been played */

			/* Change the array according to the game's rules */
			if (player[enemy].board[x][y] == SPOT_EMPTY)
				player[enemy].board[x][y] = SPOT_SHOT;
			else if (player[enemy].board[x][y] == SPOT_SHIP_HIDDEN)
			{
				player[enemy].board[x][y] = SPOT_SHIP_SHOT;
				ships_shot[turn]++;
			}

			/* Use the origina answer to send the valid move to the enemy */
			len = sprintf(buff, "Player %d has made the move: %s\n", turn + 1, orig_answer);
			send_action(PLAYER_MOVE_INFO_MESSAGE, conn[enemy]);
			send_one(buff, conn[enemy]);

			/* Print the updated boards to both players */
			print_boards(player, conn);

			/* Check if we have a winner */
			if (ships_shot[0] == 2 || ships_shot[1] == 2)
			{
				printf("Player %d has won.\n", turn + 1);
				printf("Game ended, sending EOF.\n");
				fflush(stdout);

				/* The game has ended, EOF */
				send_action(GAME_FINISHED, conn[turn]);
				send_action(GAME_FINISHED, conn[enemy]);

				send_eof(conn[0]);
				send_eof(conn[1]);

				break;
			}
		}

		/* Assign players for the next turn */
		if (turn == 0)
		{
			turn = 1;
			enemy = 0;
		}
		else
		{
			turn = 0;
			enemy = 1;
		}
	}
}

int valid_format(char answer[])
{
	char comma;
	int x, y;

	/* If sscanf does not return 3, move format is invalid */
	if (sscanf(answer, "%d%c%d", &x, &comma, &y) != 3)
		return 0;
	return 1;
}

int validate_move(Navy player[], int x, int y, int enemy)
{
	if ((x < 0 || x > 4) || (y < 0 || y>4))
		return OUT_OF_BOUNDS;
	else if (player[enemy].board[x][y] != SPOT_EMPTY && player[enemy].board[x][y] != SPOT_SHIP_HIDDEN)
		return ALREADY_SHOT;
	return MOVE_VALID;
}

/* Note that the functions below break if:
 * 1) Int size is different between platforms (2 vs 4 bytes)
 *
 * However, since the program will be supposedly compiled with
 * the same compiler and used by PCs within the last two 
 * decades, this should not be an issue.
 */

void send_action(int action, connection conn)
{
	/* Sends a game opcode to the client */

	/* Account for endianess */
	int c_action = htonl(action);

	/* Send game opcode */
	send(conn, &c_action, sizeof(c_action), 0);
}

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

int recv_one(char buff[], connection conn)
{
	/* Receives a length field, then the message from the server */

	int c_len, len, response;

	/* Receive message length */
	response = recv(conn, &c_len, sizeof(c_len), 0);

	/* If response < 1, client disconnected */
	if (response < 1)
		return -1;

	/* Account for endianess */
	len = ntohl(c_len);

	/* Receive the message */
	recv(conn, buff, len, 0);

	/* Return message length */
	return len;
}