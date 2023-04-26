# Project3
systems assignment
# NetIDs:
Akshith Dandemraju acd218
Abhinav Acharya aa

# Project Outline
Our project is tic tac toe online. We connect the players using sockets and pthreading. We are using multithreading to be able to run multiple games at once. 
So we use one thread per game and we have 2 client connections on that thread. We communicate using read and write functions. We also using select function to check for events from the clients.

Our implimentation uses these protocols:
• PLAY name
• WAIT
• BEGN role name
• MOVE role position
• MOVD role position board
• INVL reason
• RSGN
• DRAW message
• OVER outcome reason

The server and client communicate using these protocols. 

# ttts program
The Game starts off with the client sending the PLAY command with their name. Then the server responds with WAIT|0|
Once the server has found 2 active players, it will connect them and send BEGN and the other player's name.
If both players have the same name then the server sends INVD command and ends.
After they are connected they can play with MOVE command and the position on the board they would like to play.
If the move is valid the server then changes the game board and alerts both players of the new board using the MOVD command.
If one of the players wants to DRAW then can suggest it when its their turn. Then the other player can accept or reject.
When its their turn that player can also RSGN and end the game.
Once the game has ended the server will send OVER and the outcome to the client.

# Test plan
Error cases are as follows: 
Invalid message errors if a player sends a invalid message or invalid protcols, where the bytes doesn't match, then the game automatically kills that connection.

Same name case:
if there is a connection waiting for another player, and another player signs in with a name that is same as the connection currently waiting, then it sends an error message and stops that connection, however the first connection, is still working and waiting for another player with a different name. If another connection joins with a different name, then their game, starts, and any other connection can join with the names picked by those 2 because, their name is now irrelevant to other players in different games.

Game Board errors:
If a player gives a different role than his her own role, then it displays a message saying wrong role, and lets the player send a message to fix that mistake.
if a player gives a index that is out of bounds it sends an error and lets player correct their mistake
if a player gives an index that is already occupied it lets the player know and lets the player fix that mistake

Draw errors:
If all squares on the game board are occupied and there is no winner, the game should end in a draw. The server should send a "Draw" message to both players.
Resign Errors:
If a player resigns from the game, the server should end the game and send a "Resignation" message to the other player.
Multithreading Errors:
If multiple players send messages simultaneously, the server should handle each message in a thread-safe manner to prevent data corruption or race conditions.
If a player disconnects from the game unexpectedly, the server should detect the disconnection and terminate the connection with the disconnected player, and notify the other player of the disconnection with a "Player disconnected" message.
Max clients possible is 100
Max command length is 256

WE DID THE 120 point version of the assignment