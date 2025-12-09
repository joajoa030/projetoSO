#include "board.h"
#include "display.h"
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define LOAD_BACKUP 3
#define CREATE_BACKUP 4

void screen_refresh(board_t * game_board, int mode) {
    debug("REFRESH\n");
    draw_board(game_board, mode);
    refresh_screen();
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
}

int play_board(board_t * game_board) {
    pacman_t* pacman = &game_board->pacmans[0];
    command_t* play;
    if (pacman->n_moves == 0) { // if is user input
        command_t c; 
        c.command = get_input();

        if(c.command == '\0')
            return CONTINUE_PLAY;

        c.turns = 1;
        play = &c;
    }
    else { // else if the moves are pre-defined in the file
        // avoid buffer overflow wrapping around with modulo of n_moves
        // this ensures that we always access a valid move for the pacman
        play = &pacman->moves[pacman->current_move%pacman->n_moves];
    }

    debug("KEY %c\n", play->command);

    if (play->command == 'Q') {
        return QUIT_GAME;
    }

    int result = move_pacman(game_board, 0, play);
    if (result == REACHED_PORTAL) {
        // Next level
        return NEXT_LEVEL;
    }

    if(result == DEAD_PACMAN) {
        return QUIT_GAME;
    }
    
    for (int i = 0; i < game_board->n_ghosts; i++) {
        ghost_t* ghost = &game_board->ghosts[i];
        // avoid buffer overflow wrapping around with modulo of n_moves
        // this ensures that we always access a valid move for the ghost
        move_ghost(game_board, i, &ghost->moves[ghost->current_move%ghost->n_moves]);
    }

    if (!game_board->pacmans[0].alive) {
        return QUIT_GAME;
    }      

    return CONTINUE_PLAY;  
}

int main(int argc, char** argv) {
    DIR *directory;
    struct dirent *entry;
    int n=0;
    char **list_lvl = NULL;
    directory = opendir("lvl");
    if(directory == NULL){
        printf("ERROR DIRECTORY\n");
        return 1;
    }
    
    while((entry = readdir(directory)) != NULL){
        const char *nome_ficheiro = entry->d_name;
        size_t len = strlen(nome_ficheiro);
        if (len >= 4 && strcmp(nome_ficheiro + len - 4, ".lvl") == 0) {
            
            // aumentar a lista dinamicamente
            char **nova_lista = realloc(list_lvl, (n + 1) * sizeof(char*));
            if (!nova_lista) {
                perror("realloc");
                closedir(directory);
                return 1; // retorna o que conseguir
            }

            list_lvl = nova_lista;

            // guardar o nome
            list_lvl[n] = malloc(strlen(nome_ficheiro) + 1);
            strcpy(list_lvl[n], nome_ficheiro);

            n++;
        }   
    }


    if (argc != 2) {
        printf("Usage: %s <level_directory>\n", argv[0]);
        // TODO receive inputs
    }

    // Random seed for any random movements
    srand((unsigned int)time(NULL));

    open_debug_file("debug.log");

    terminal_init();
    
    int accumulated_points = 0;
    bool end_game = false;
    board_t game_board;
    
  
        for(int i = 0; i < n && !end_game; i++){
            
            char path[512];
            snprintf(path, sizeof(path), "lvl/%s", list_lvl[i]);

            parser(path, &game_board, accumulated_points);
            draw_board(&game_board, DRAW_MENU);
            refresh_screen();
            sleep_ms(game_board.tempo);//primeira jogada
            while(true) {
                int result = play_board(&game_board); 

                if(result == NEXT_LEVEL) {
                    screen_refresh(&game_board, DRAW_WIN);
                    sleep_ms(game_board.tempo);
                    break;
                }

                if(result == QUIT_GAME) {
                    screen_refresh(&game_board, DRAW_GAME_OVER); 
                    sleep_ms(game_board.tempo);
                    end_game = true;
                    break;
                }
        
                screen_refresh(&game_board, DRAW_MENU); 

                accumulated_points = game_board.pacmans[0].points;      
            }
            print_board(&game_board);
            unload_level(&game_board);
        }       
    

    terminal_cleanup();

    close_debug_file();

    return 0;
}
