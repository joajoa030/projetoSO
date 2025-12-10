#include "board.h"
#include "display.h"
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <sys/wait.h>
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
    if (play->command == 'G') {
        
        game_board->backup_exists ++;
        debug("BACKUP REQUESTED %d\n", game_board->backup_exists);
        if(game_board->backup_exists==1){
            debug("play_board CREATE BACKUP\n");
            return CREATE_BACKUP;
        }  
        return CONTINUE_PLAY; 
    }
    int result = move_pacman(game_board, 0, play);
    if (result == REACHED_PORTAL) {
        // Next level
        return NEXT_LEVEL;
    }

    if(result == DEAD_PACMAN) {
        if(game_board->backup_exists != 1){
            debug("toiros\n");
            return QUIT_GAME;
            
        }
        else{
            debug("LOAD BACKUP\n");
            result = LOAD_BACKUP;
            game_board->backup_exists=0;
        }
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
void child_win(board_t *game_board) {
    screen_refresh(game_board, DRAW_WIN);
    sleep_ms(game_board->tempo);
    kill(getppid(), SIGTERM);
}
int main(int argc, char** argv) {
    DIR *directory;
    struct dirent *entry;
    int n=0;
    int backup_exists=0;
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
    int current_level=1;
    int win=0;
    int child=0;
    int accumulated_points = 0;
    bool end_game = false;
    board_t game_board;
    pid_t pid = -1;
    game_board.backup_exists =0;
  
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
                    current_level++;
                    debug("NEXT LEVEL %d\n", current_level);
                    if(current_level==n+1){
                        win=1;
                        end_game=true;
                        if(child==1){
                            debug("CHILD WIN\n");
                            child_win(&game_board);
                        }
                        break;
                    }
                    else{
                        screen_refresh(&game_board, DRAW_WIN);
                        sleep_ms(game_board.tempo);
                        break;

                    }
                    
                    
                    
                }

                if(result == QUIT_GAME) {
                    if (backup_exists) {
                        
                        screen_refresh(&game_board, DRAW_MENU);
                        if(child==1){
                            if(win==1){
                                debug("CHILD WIN\n");
                                screen_refresh(&game_board, DRAW_WIN);
                                sleep_ms(game_board.tempo);
                            }
                            else{
                                debug("CHILD QUIT\n");
                                child=0;
                                exit(0);
                            }
                            
                        }
                        else{
                            screen_refresh(&game_board, DRAW_GAME_OVER); 
                            sleep_ms(game_board.tempo);
                            end_game = true;
                            break;
                        }
                        
                    }
                    else {
                        screen_refresh(&game_board, DRAW_GAME_OVER); 
                        sleep_ms(game_board.tempo);
                        end_game = true;
                        break;
                    }
                }
                
                if (result == CREATE_BACKUP) {
                    if(game_board.backup_exists == 1){
                        debug("main CREATE BACKUP\n");
                        backup_exists = 1;
                        pid = fork();
                        if (pid == 0) {
                            child=1;
                            // FILHO → cria o backup e sai
                            screen_refresh(&game_board, DRAW_MENU);
                        } else if (pid > 0) {

                            // PAI → continua o jogo
                            backup_exists = 1;
                            wait(0);

                            backup_exists = 0;
                            game_board.backup_exists = 0;
                            screen_refresh(&game_board, DRAW_MENU);
                            continue;
                        }
                    }
                }

                if (result == LOAD_BACKUP) {
                    if (backup_exists) {
                        screen_refresh(&game_board, DRAW_MENU);
                        continue;
                    } 
                    else {
                    return QUIT_GAME;
                    }
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
