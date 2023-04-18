#include <stdio.h>

int main() {
    // Initialize the game board
    char board[3][3] = {
        {'.', '.', '.'},
        {'.', '.', '.'},
        {'.', '.', '.'}
    };
    
    // Print the initial game board
    printf("  1 2 3\n");
    printf(" -------\n");
    printf("1|%c|%c|%c|\n", board[0][0], board[0][1], board[0][2]);
    printf(" -------\n");
    printf("2|%c|%c|%c|\n", board[1][0], board[1][1], board[1][2]);
    printf(" -------\n");
    printf("3|%c|%c|%c|\n", board[2][0], board[2][1], board[2][2]);
    printf(" -------\n");

    // Start the game loop
    int player = 1;
    int row, col;
    char mark;
    int gameOver = 0;
    while (!gameOver) {
        // Get the player's move
        printf("Player %d, enter row (1-3) and column (1-3) separated by a space: ", player);
        scanf("%d %d", &row, &col);
        row--;
        col--;
        
        // Check if the move is valid
        if (row < 0 || row > 2 || col < 0 || col > 2) {
            printf("Invalid move. Row and column must be between 1 and 3.\n");
            continue;
        }
        if (board[row][col] != '.') {
            printf("Invalid move. Cell is already occupied.\n");
            continue;
        }
        
        // Update the game board
        if (player == 1) {
            mark = 'X';
        } else {
            mark = 'O';
        }
        board[row][col] = mark;
        
        // Print the updated game board
        printf("  1 2 3\n");
        printf(" -------\n");
        printf("1|%c|%c|%c|\n", board[0][0], board[0][1], board[0][2]);
        printf(" -------\n");
        printf("2|%c|%c|%c|\n", board[1][0], board[1][1], board[1][2]);
        printf(" -------\n");
        printf("3|%c|%c|%c|\n", board[2][0], board[2][1], board[2][2]);
        printf(" -------\n");
        
        // Check if the game is over
        // TODO: add game over conditions here
        
        
        // Switch to the other player
        if (player == 1) {
            player = 2;
        } else {
            player = 1;
        }
    }
    
    return 0;
}
