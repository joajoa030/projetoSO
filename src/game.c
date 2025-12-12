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
#include <pthread.h>
#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2
#define LOAD_BACKUP 3
#define CREATE_BACKUP 4
typedef struct {
    board_t *board;
    int ghost_index;
} ghost_arg_t;

void *thread_ghost(void *arg);   // <--- AQUI

pthread_mutex_t board_mutex;

int quit_play = 0;
int quit = 0;

void screen_refresh(board_t * game_board, int mode) {
    pthread_mutex_lock(&board_mutex);
    debug("REFRESH\n");
    draw_board(game_board, mode);
    refresh_screen();
    pthread_mutex_unlock(&board_mutex);

    if (game_board->tempo != 0)
        sleep_ms(game_board->tempo);
}
void *thread_ghost(void *arg) {
    ghost_arg_t *garg = (ghost_arg_t*) arg;
    board_t *board = garg->board;
    int id = garg->ghost_index;
    free(garg);

    while (!quit) {

        pthread_mutex_lock(&board_mutex);
        if (!board->pacmans[0].alive) { 
            pthread_mutex_unlock(&board_mutex);
            break;
        }

        ghost_t *g = &board->ghosts[id];

        if (id < board->n_ghosts && g->n_moves > 0) {
            command_t *cmd =
                &g->moves[g->current_move % g->n_moves];

            move_ghost(board, id, cmd);

            g->current_move =
                (g->current_move + 1) % g->n_moves;
        }

        pthread_mutex_unlock(&board_mutex);
        screen_refresh(board, DRAW_MENU);

    }

    return NULL;
} 



int play_board(board_t * game_board) {
    pacman_t* pacman = &game_board->pacmans[0];
    pthread_mutex_lock(&board_mutex);
    int pacman_alive = game_board->pacmans[0].alive;
    int backup_flag = game_board->backup_exists;
    pthread_mutex_unlock(&board_mutex);

    if (!pacman_alive) {
        if (backup_flag == 1) {
            debug("play_board: pacman morto → LOAD_BACKUP\n");
            return LOAD_BACKUP;
        } else {
            debug("play_board: pacman morto → QUIT_GAME\n");
            return QUIT_GAME;
        }
    }
    command_t* play;
    command_t c; 
    if (pacman->n_moves == 0) { // if is user input
        debug("gui\n");
        c.command = get_input();

        if(c.command == '\0')
            return CONTINUE_PLAY;

        c.turns = 1;
        play = &c;
    }
    else { // else if the moves are pre-defined in the file
        // avoid buffer overflow wrapping around with modulo of n_moves
        // this ensures that we always access a valid move for the pacman
        debug("piu\n");
        play = &pacman->moves[pacman->current_move%pacman->n_moves];
        
    }

    debug("KEY %c\n", play->command);

    if (play->command == 'Q') {
        quit_play = 1;
        quit=1; 
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
    pthread_mutex_lock(&board_mutex);
    int result = move_pacman(game_board, 0, play);
    pthread_mutex_unlock(&board_mutex);
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
    directory = opendir(argv[1]);
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
    pthread_mutex_init(&board_mutex, NULL);
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
            
            pthread_t *ghost_threads = malloc(sizeof(pthread_t) * game_board.n_ghosts);

            for (int g = 0; g < game_board.n_ghosts; ++g) {
            ghost_arg_t *garg = malloc(sizeof(ghost_arg_t));
            if (!garg) {
                perror("malloc garg");
                // se falhar, podes decidir continuar sem esse fantasma
                continue;
            }
            garg->board = &game_board;
            garg->ghost_index = g;
            if (pthread_create(&ghost_threads[g], NULL, thread_ghost, garg) != 0) {
                debug("erro pthread_create ghost %d\n", g);
                free(garg);
            }
}
            pthread_mutex_lock(&board_mutex);
            draw_board(&game_board, DRAW_MENU);
            refresh_screen();
            pthread_mutex_unlock(&board_mutex);
            sleep_ms(game_board.tempo);//primeira jogada
            
            while(true) {
                
                
                int result = play_board(&game_board); 

                if(result == NEXT_LEVEL) {
                    current_level++;
                    debug("NEXT LEVEL %d\n", current_level);
                    for (int t = 0; t < game_board.n_ghosts; t++) {
                        pthread_cancel(ghost_threads[t]);
                    }

                    // 2) ESPERAR ELAS TERMINAREM
                    for (int t = 0; t < game_board.n_ghosts; t++) {
                        pthread_join(ghost_threads[t], NULL);
                    }
                    
                    if(current_level==n+1){
                        win=1;
                        end_game=true;
                        
                        if(child==1){
                            debug("CHILD WIN\n");
                            child_win(&game_board);
                        }
                        
                        else{
                            screen_refresh(&game_board, DRAW_WIN);
                        }
                        
                        break;
                    }
                    
                    else{
                        screen_refresh(&game_board, DRAW_WIN);
                        break;

                    }    
                }

                if(result == QUIT_GAME) {
                    
                    // Se existe um backup activo, usamos a lógica específica
                    if (game_board.backup_exists || backup_exists) {
                        // mostra menu e decide comportamento consoante se é filho
                        screen_refresh(&game_board, DRAW_MENU);

                        if (child == 1) {
                            // No filho: se ganhou, mostra WIN, senão se foi Q sai, caso contrário apenas sai
                            if (win == 1) {
                                debug("CHILD WIN\n");
                                screen_refresh(&game_board, DRAW_WIN);
                            } else {
                                if (quit_play == 1) {
                                    // jogador pressionou Q no filho -> mostrar game over e sair
                                    screen_refresh(&game_board, DRAW_GAME_OVER);
                                    // limpa threads do filho e termina processo do filho
                                    quit = 1;
                                    if (ghost_threads) {
                                        for (int t = 0; t < game_board.n_ghosts; t++)
                                            pthread_cancel(ghost_threads[t]);
                                        for (int t = 0; t < game_board.n_ghosts; t++)
                                            pthread_join(ghost_threads[t], NULL);
                                        free(ghost_threads);
                                        ghost_threads = NULL;
                                    }
                                    exit(0);
                                }
                                debug("CHILD QUIT\n");
                                // se chegou aqui, sai também
                                if (ghost_threads) {
                                    for (int t = 0; t < game_board.n_ghosts; t++)
                                        pthread_cancel(ghost_threads[t]);
                                    for (int t = 0; t < game_board.n_ghosts; t++)
                                        pthread_join(ghost_threads[t], NULL);
                                    free(ghost_threads);
                                    ghost_threads = NULL;
                                }
                                exit(0);
                            }
                        } else {
                            // Pai com backup activo: mostra GAME_OVER e termina o loop
                            screen_refresh(&game_board, DRAW_GAME_OVER);
                            end_game = true;
                            // limpa threads do pai
                            quit = 1;
                            if (ghost_threads) {
                                for (int t = 0; t < game_board.n_ghosts; t++)
                                    pthread_cancel(ghost_threads[t]);
                                for (int t = 0; t < game_board.n_ghosts; t++)
                                    pthread_join(ghost_threads[t], NULL);
                                free(ghost_threads);
                                ghost_threads = NULL;
                            }
                            break;
                        }
                    } else {
                        // Não há backup activo: comportamento simples e consistente:
                        // - Se pacman está morto, garante game over
                        // - Se jogador pressionou Q (quit_play==1), termina imediatamente
                        pthread_mutex_lock(&board_mutex);
                        int pacman_alive = game_board.pacmans[0].alive;
                        pthread_mutex_unlock(&board_mutex);

                        // Se foi o jogador a pedir saída => fechar
                        if (quit_play == 1) {
                            debug("PLAYER REQUESTED QUIT\n");
                            // mostrar game over, limpar threads e sair
                            screen_refresh(&game_board, DRAW_GAME_OVER);
                            quit = 1;
                            if (ghost_threads) {
                                for (int t = 0; t < game_board.n_ghosts; t++)
                                    pthread_cancel(ghost_threads[t]);
                                for (int t = 0; t < game_board.n_ghosts; t++)
                                    pthread_join(ghost_threads[t], NULL);
                                free(ghost_threads);
                                ghost_threads = NULL;
                            }
                            end_game = true;
                            break;
                        }

                        // Caso pacman esteja morto — game over também
                        if (!pacman_alive) {
                            debug("PACMAN DEAD -> GAME OVER\n");
                            quit = 1;
                            if (ghost_threads) {
                                for (int t = 0; t < game_board.n_ghosts; t++)
                                    pthread_cancel(ghost_threads[t]);
                                for (int t = 0; t < game_board.n_ghosts; t++)
                                    pthread_join(ghost_threads[t], NULL);
                                free(ghost_threads);
                                ghost_threads = NULL;
                            }
                            screen_refresh(&game_board, DRAW_GAME_OVER);
                            end_game = true;
                            break;
                        }

                        // Safety fallback: se chegámos aqui sem condição clara, fechamos de qualquer forma.
                        debug("QUIT_GAME sem backup e pacman vivo -> encerrar por segurança\n");
                        quit = 1;
                        if (ghost_threads) {
                            for (int t = 0; t < game_board.n_ghosts; t++)
                                pthread_cancel(ghost_threads[t]);
                            for (int t = 0; t < game_board.n_ghosts; t++)
                                pthread_join(ghost_threads[t], NULL);
                            free(ghost_threads);
                            ghost_threads = NULL;
                        }
                        screen_refresh(&game_board, DRAW_GAME_OVER);
                        end_game = true;
                        break;
                    }
                }
                
                if (result == CREATE_BACKUP) {
                    
                    if(game_board.backup_exists == 1){
                        debug("main CREATE BACKUP\n");
                        backup_exists = 1;

                        // --- novas linhas ---
                        quit = 1;  // parar as threads
                        for (int t = 0; t < game_board.n_ghosts; t++)
                            pthread_cancel(ghost_threads[t]);

                        for (int t = 0; t < game_board.n_ghosts; t++)
                            pthread_join(ghost_threads[t], NULL);

                        free(ghost_threads);
                        ghost_threads = NULL;
                        // ---------------------

                        pid = fork();
                        
                        if (pid == 0) {   // FILHO
                            child=1;
                            quit = 0;
                            quit_play = 0;
                            backup_exists = 1;
                            game_board.backup_exists = 1;
                            terminal_init();
                            // NOVO ARRAY de threads (não herdar o do pai)
                            ghost_threads = malloc(sizeof(pthread_t) * game_board.n_ghosts);

                            for (int g = 0; g < game_board.n_ghosts; ++g) {
                                ghost_arg_t *garg = malloc(sizeof(ghost_arg_t));
                                garg->board = &game_board;
                                garg->ghost_index = g;

                                if (pthread_create(&ghost_threads[g], NULL, thread_ghost, garg) != 0) {
                                    debug("FILHO: erro pthread_create ghost %d\n", g);
                                    free(garg);
                                }
                            }

                            continue;
                        }
                        
                        else if (pid > 0) {   // PAI = processo de backup
                            quit = 1;
                            backup_exists = 1;

                            // O pai NÃO PODE mexer no terminal
                            terminal_cleanup();

                            // Fica parado à espera do filho
                            int status;
                            waitpid(pid, &status, 0);
                        }
                    }
                }

                if (result == LOAD_BACKUP) {
                    
                    if (backup_exists) {
                        screen_refresh(&game_board, DRAW_MENU);
                        continue;
                    } 
                    
                    else{
                        return QUIT_GAME;
                    }
                }

                accumulated_points = game_board.pacmans[0].points;      
            
            }
            
            print_board(&game_board);
            if (ghost_threads) {
                for (int i = 0; i < game_board.n_ghosts; ++i) {
                    pthread_cancel(ghost_threads[i]);
                }
                for (int i = 0; i < game_board.n_ghosts; ++i) {
                    pthread_join(ghost_threads[i], NULL);
                }
                free(ghost_threads);
                ghost_threads = NULL;
            }
            unload_level(&game_board);
            
        }       
    terminal_cleanup();
    pthread_mutex_destroy(&board_mutex);    
    close_debug_file();    
    return 0;    
    
    
}
