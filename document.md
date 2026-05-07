The bot connects to an IRC server, joins multiple channels, and interacts with users based on predefined narratives loaded from a catalogue.
The implementation utilizes `fork()`, `pipe()`, shared memory, signals, and semaphores as required.
The bot operates as a single main process for IRC communication.
- **Narrative Loading:** A child process is forked at startup (and on admin command) to parse narrative files and load them into shared memory.
- **IPC Mechanisms:**
    - `fork()`: Used to create the narrative loader child process.
    - `pipe()`: (If any other utility children were added for specific tasks and needed to communicate back to the main bot process. Not used for the primary IRC channel handling in this single-process model, but used if, for example, an admin command forked a long task).
    - `Shared Memory`: Stores the narrative catalogue for access by the main bot process after being populated by the loader child.
    - `Signals`: `SIGINT` for graceful shutdown of the main bot and `SIGCHLD` to reap the narrative loader child. Admin `!shutdown` command also uses `kill(getpid(), SIGINT)`.
    - `Semaphores`: Used for mutual exclusion to protect the shared memory narrative block during read (by main bot) and write/reload (by narrative loader child) operations.

Project Structure:
    src/main.c - main bot process, IRC loop, forks
    src/config_parser.c - Configuration file parsing
    src/irc_client.c/h - IRC protocol and socket communication
    src/bot_logic.c/h - Message processing, narrative matching, admin commands
    src/narrative_parser.c/h - Parsing narrative files
    config/bot.conf - Main bot configuration
    config/narratives - directory for prompts that the user can imput and answers that the bot gives
    Makefile - Build script

To build, compile and run
sudo make
./irc_chatbot

To reset the compiled ./irc_chatbot file run sudo make clean and run the before mentioned commands again.


The challenge was integrating all required IPC mechanisms (fork, pipe, shared memory, semaphores) meaningfully into a single primary bot instance. 
This was achieved by using fork for the narrative loader, shared memory for the narratives, and semaphores to protect shared memory. 
Pipe would be used if other forked utilities were added

Bot was tested via irssi by connecting to the provided server ip and port and connecting to the channels in the bot.conf config file
