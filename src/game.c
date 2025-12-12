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

void *thread_ghost(void *arg);

pthread_mutex_t board_mutex;
int quit_play = 0;
int quit = 0;

void screen_refresh(board_t * game_board, int mode) {
    pthread_mutex_lock(&board_mutex);
    draw_board(game_board, mode);
    refresh_screen();
    pthread_mutex_unlock(&board_mutex);
    if (game_board->tempo != 0) sleep_ms(game_board->tempo);
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
            command_t *cmd = &g->moves[g->current_move % g->n_moves];
            move_ghost(board, id, cmd);
            g->current_move = (g->current_move ) % g->n_moves;
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
            return LOAD_BACKUP;
        } else {
            return QUIT_GAME;
        }
    }

    command_t* play;
    command_t c;

    if (pacman->n_moves == 0) { // if is user input
        c.command = get_input();
        if(c.command == '\0') return CONTINUE_PLAY;
        c.turns = 1;
        play = &c;
    } else {
        play = &pacman->moves[pacman->current_move%pacman->n_moves];
    }


    if (play->command == 'Q') {
        quit_play = 1;
        quit=1;
        return QUIT_GAME;
    }

    if (play->command == 'G') {
        if (pacman->n_moves > 0) {
            pthread_mutex_lock(&board_mutex);
            pacman->current_move++;
            pthread_mutex_unlock(&board_mutex);
        }
        game_board->backup_exists ++;
        if(game_board->backup_exists==1){
            return CREATE_BACKUP;
        }
        return CONTINUE_PLAY;
    }

    pthread_mutex_lock(&board_mutex);
    int result = move_pacman(game_board, 0, play);
    pthread_mutex_unlock(&board_mutex);

    if (result == REACHED_PORTAL) {
        return NEXT_LEVEL;
    }

    if(result == DEAD_PACMAN) {
        if(game_board->backup_exists != 1){
            return QUIT_GAME;
        }
        else{
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
            char **nova_lista = realloc(list_lvl, (n + 1) * sizeof(char*));
            if (!nova_lista) {
                perror("realloc");
                closedir(directory);
                return 1;
            }
            list_lvl = nova_lista;
            list_lvl[n] = malloc(strlen(nome_ficheiro) + 1);
            strcpy(list_lvl[n], nome_ficheiro);
            n++;
        }
    }

    if (argc != 2) {
        printf("Usage: %s <level_directory>\n", argv[0]);
    }

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
        quit = 0; 
        pthread_t *ghost_threads = malloc(sizeof(pthread_t) * game_board.n_ghosts);
        for (int g = 0; g < game_board.n_ghosts; ++g) {
            ghost_arg_t *garg = malloc(sizeof(ghost_arg_t));
            if (!garg) {
                perror("malloc garg");
                continue;
            }
            garg->board = &game_board;
            garg->ghost_index = g;

            if (pthread_create(&ghost_threads[g], NULL, thread_ghost, garg) != 0) {
                free(garg);
            }
        }

        pthread_mutex_lock(&board_mutex);
        draw_board(&game_board, DRAW_MENU);
        refresh_screen();
        sleep_ms(game_board.tempo);
        pthread_mutex_unlock(&board_mutex);
        
        

        while(true) {

            int result = play_board(&game_board);
            sleep_ms(game_board.tempo);
            if(result == NEXT_LEVEL) {
                current_level++;
                quit = 1;    


                if(current_level==n+1){
                    win=1;
                    end_game=true;
                    if(child==1){
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

                if (game_board.backup_exists) {
                    screen_refresh(&game_board, DRAW_MENU);

                    if(child==1){
                        if(win==1){
                            screen_refresh(&game_board, DRAW_WIN);
                        }
                        else{
                            if(quit_play==1){
                                screen_refresh(&game_board, DRAW_GAME_OVER);
                                end_game = true;
                                kill(getppid(), SIGTERM);
                                terminal_cleanup();
                                exit(0);
                            }
                            child=0;
                            exit(0);
                        }
                    }
                    else{
                        screen_refresh(&game_board, DRAW_GAME_OVER);
                        end_game = true;
                        break;
                    }
                }
                else{
                    screen_refresh(&game_board, DRAW_GAME_OVER);
                    end_game = true;
                    break;
                }

            }
            else{
                pthread_mutex_lock(&board_mutex);
                int pacman_alive = game_board.pacmans[0].alive;
                pthread_mutex_unlock(&board_mutex);

                if (!pacman_alive) {
                    quit=1;
                    screen_refresh(&game_board, DRAW_GAME_OVER);
                    end_game = true;
                    break;
                }
            }

            if (result == CREATE_BACKUP) {
                // cria backup/clonagem: pai cria fork -> filho continua a jogar
                pthread_mutex_lock(&board_mutex);
                game_board.backup_exists = 1;
                pthread_mutex_unlock(&board_mutex);

                // Parar threads do pai
                quit = 1;
                for (int t = 0; t < game_board.n_ghosts; t++)
                    pthread_join(ghost_threads[t], NULL);

                free(ghost_threads);
                ghost_threads = NULL;

                // Pai limpa o terminal antes do fork para ceder controlo ao filho
                
                terminal_cleanup();
                pid = fork();
                if (pid < 0) {
                    perror("fork");
                    // tentar recuperar: re-inicializar terminal e continuar
                    terminal_init();
                    quit = 0;
                    continue;
                }

                if (pid == 0) {   // FILHO - clone que continua o jogo
                    child = 1;
                    quit = 0;
                    quit_play = 0;
                    game_board.backup_exists = 1;

                    // Filhos re-inicializam o terminal e recriam threads
                    terminal_init();
                    draw_board(&game_board, DRAW_MENU);
                    refresh_screen();
                    ghost_threads = malloc(sizeof(pthread_t) * (game_board.n_ghosts > 0 ? game_board.n_ghosts : 1));
                    if (ghost_threads) {
                        for (int g = 0; g < game_board.n_ghosts; ++g) {
                            ghost_arg_t *garg = malloc(sizeof(ghost_arg_t));
                            if (!garg) {
                                perror("malloc garg (filho)");
                                continue;
                            }
                            garg->board = &game_board;
                            garg->ghost_index = g;
                            if (pthread_create(&ghost_threads[g], NULL, thread_ghost, garg) != 0) {
                                free(garg);
                            }
                        }
                    }
                    // Filho volta imediatamente ao loop e continua o jogo
                    continue;
                } else { // PAI: espera que o filho termine (waitpid bloqueante)
                    int status;
                    waitpid(pid, &status, 0);

                    // Filho terminou; pai recupera terminal e estado
                    terminal_init();
                    draw_board(&game_board, DRAW_MENU);
                    refresh_screen();

                    // Reset backup flag no pai (não há clone activo agora)
                    pthread_mutex_lock(&board_mutex);
                    game_board.backup_exists = 0;
                    pthread_mutex_unlock(&board_mutex);

                    // Pai precisa recriar threads para continuar o nível original
                    ghost_threads = malloc(sizeof(pthread_t) * (game_board.n_ghosts > 0 ? game_board.n_ghosts : 1));
                    if (ghost_threads) {
                        quit = 0;
                        for (int g = 0; g < game_board.n_ghosts; ++g) {
                            ghost_arg_t *garg = malloc(sizeof(ghost_arg_t));
                            if (!garg) {
                                perror("malloc garg (pai re-cria)");
                                continue;
                            }
                            garg->board = &game_board;
                            garg->ghost_index = g;
                            if (pthread_create(&ghost_threads[g], NULL, thread_ghost, garg) != 0) {
                                free(garg);
                            }
                        }
                    }
                    // depois de waitpid e recriação, o pai continua naturalmente o loop
                    continue;
                }


            }

            if (result == LOAD_BACKUP) {
                if (game_board.backup_exists) {
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

        for (int i = 0; i < game_board.n_ghosts; ++i) {
            pthread_join(ghost_threads[i], NULL);
        }

        free(ghost_threads);
        ghost_threads = NULL;

        unload_level(&game_board);
    }

    terminal_cleanup();
    pthread_mutex_destroy(&board_mutex);
    close_debug_file();

    return 0;
}
