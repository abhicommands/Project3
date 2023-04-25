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