#include "board.h"
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#define STRIDE 128

int parser(char* filename, board_t* game_board, int accumulated_points) {
    sprintf(game_board->level_name, "%s", filename);
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open error");
        return EXIT_FAILURE;
    }

    char buffer[STRIDE];
    char* content = NULL;
    size_t content_size = 0;

    /* read the contents of the file */
    while (1) {
        int bytes_read = read(fd, buffer, STRIDE);

        if (bytes_read < 0) {
            perror("read error");
            free(content);
            close(fd);
            return EXIT_FAILURE;
        }

        /* if we read 0 bytes, we're done */
        if (bytes_read == 0) {
            break;
        }

        // Allocate memory to store the new data
        char* new_content = realloc(content, content_size + bytes_read + 1);
        if (!new_content) {
            perror("memory allocation error");
            free(content);
            close(fd);
            return EXIT_FAILURE;
        }

        content = new_content;

        // Copy the buffer into the content
        memcpy(content + content_size, buffer, bytes_read);
        content_size += bytes_read;
        content[content_size] = '\0'; // Null-terminate the string
    }
    // Variáveis para manter o estado do strtok_r (necessário para loops aninhados)
    char *saveptr1;
    char *saveptr2;
    // Usar strtok_r para o loop externo
    char* line = strtok_r(content, "\n", &saveptr1);

    int line_count = 0;
    game_board->n_ghosts = 0;
    int ispacman = 0;

    while(line) {
        // Ignorar comentários
        if (line[0] == '#'){ 
            line = strtok_r(NULL, "\n", &saveptr1);
            continue;
        }
        
        if(strncmp(line, "DIM", 3) == 0){
            int width, height;
            sscanf(line, "DIM %d %d", &width, &height);
            game_board->width = width;
            game_board->height = height;
            // Importante: verificar se o calloc não falhou
            game_board->board = calloc(width * height, sizeof(board_pos_t));
        }
        
        else if(strncmp(line, "TEMPO", 5) == 0){
            int time_val;
            sscanf(line, "TEMPO %d", &time_val);
            game_board->tempo = time_val; // Correção: Atribuição DEPOIS de ler
        }

        else if(strncmp(line, "PAC", 3) == 0){
            ispacman = 1;
            game_board->n_pacmans = 1;
            game_board->pacmans = calloc(game_board->n_pacmans, sizeof(pacman_t));
            
            // Correção: Usar um array, não um char único
            char pacman_file[256]; 
            sscanf(line, "PAC %s", pacman_file);
            
            // Passar o array, não o endereço do array (&array já é um ponteiro neste contexto, mas &char funciona se mudar a assinatura)
            parserPACMON(pacman_file, game_board, ispacman, accumulated_points); 
        }
        
        else if(strncmp(line, "MON", 3) == 0){
            ispacman = 0;
            char *monster_list_part = line + 4;
            
            // Usar strtok_r com um segundo saveptr para não quebrar o loop externo
            char* monster_token = strtok_r(monster_list_part, " ", &saveptr2);
            while(monster_token){
                game_board->n_ghosts++; // Incrementar contador

                game_board->ghosts = realloc(game_board->ghosts, game_board->n_ghosts * sizeof(ghost_t));
                
                parserPACMON(monster_token, game_board, ispacman, accumulated_points);
                monster_token = strtok_r(NULL, "\r", &saveptr2);
            }
        }
        
        else {
            
            // Parse do mapa
            // Proteção para não estourar o buffer se houver mais linhas que height
            if (line_count < game_board->height) {
                for(int j=0; j < game_board->width && line[j] != '\0'; j++){
                    if((line[j] == 'o' )&& game_board->board[line_count * game_board->width + j].content != 'P' && (game_board->board[line_count * game_board->width + j].content != 'M'&& game_board->board[line_count * game_board->width + j].has_portal == 0)){
                        game_board->board[line_count * game_board->width + j].content = ' '; // Dot
                        game_board->board[line_count * game_board->width + j].has_dot = 1;
                        game_board->board[line_count * game_board->width + j].has_portal = 0;
                    }
                    else if((line[j] == '@')&& game_board->board[line_count * game_board->width + j].content != 'P' && (game_board->board[line_count * game_board->width + j].content == 'M'&& game_board->board[line_count * game_board->width + j].has_portal == 0)){
                        game_board->board[line_count * game_board->width + j].content = ' ';
                        game_board->board[line_count * game_board->width + j].has_portal = 1;
                        game_board->board[line_count * game_board->width + j].has_dot = 0;
                    }
                    else if((line[j] == 'X')&& game_board->board[line_count * game_board->width + j].content != 'P' && game_board->board[line_count * game_board->width + j].content != 'M'){
                        game_board->board[line_count * game_board->width + j].content = 'W';
                        game_board->board[line_count * game_board->width + j].has_dot = 0;
                        game_board->board[line_count * game_board->width + j].has_portal = 0;
                    }
                        
                }
                line_count++;
            }
        }  
            
        // Avançar para a próxima linha usando o saveptr1 do loop externo
        line = strtok_r(NULL, "\n", &saveptr1);
    }
    
    /* At this point, the variable `content` contains the entire file content */
    /* clean up */
    free(content);
    close(fd);
    return EXIT_SUCCESS;
}




int parserPACMON(char* filename, board_t* board, int ispacman, int accumulated_points) {
    char temp[256]; // Buffer temporário para evitar sobrescrever o original
    sprintf(temp, "./lvl/%s", filename);
    char realfilename[MAX_FILENAME+5];
    strcpy(realfilename, temp); // Copia o resultado para `filename`
    int fd = open(realfilename, O_RDONLY);
    if (fd < 0) {
        perror("open error");
        return EXIT_FAILURE;
    }

    char buffer[STRIDE];
    char* content = NULL;
    size_t content_size = 0;

    /* read the contents of the file */
    while (1) {
        int bytes_read = read(fd, buffer, STRIDE);

        if (bytes_read < 0) {
            perror("read error");
            free(content);
            close(fd);
            return EXIT_FAILURE;
        }

        /* if we read 0 bytes, we're done */
        if (bytes_read == 0) {
            break;
        }

        // Allocate memory to store the new data
        char* new_content = realloc(content, content_size + bytes_read + 1);
        if (!new_content) {
            perror("memory allocation error");
            free(content);
            close(fd);
            return EXIT_FAILURE;
        }

        content = new_content;

        // Copy the buffer into the content
        memcpy(content + content_size, buffer, bytes_read);
        content_size += bytes_read;
        content[content_size] = '\0'; // Null-terminate the string
    }
    char* token = strtok(content, "\n");
    int line = 0, col = 0; // Declare and initialize line and col variables
    int wait_moves=0;
    int wait_turns=0;
    int nmoves=0;
    int nmovesmonster=0;
    while(token){
        if (token[0] == '#'){ 
            token = strtok(NULL, "\n");
            
        }
        
        else if(strncmp(token,"PASSO",5) == 0){
             
            sscanf(token,"PASSO %d",&wait_moves);
            token = strtok(NULL, "\n");
        }
        
        else if (strncmp(token,"POS",3) == 0){
            sscanf(token,"POS %d %d", &col , &line);
            token = strtok(NULL, "\n");
        }
        
        else if(token[0] == 'W'){
           
            if(ispacman){
                  
                board->pacmans[0].moves[nmoves].command = 'W';
                board->pacmans[0].moves[nmoves].turns = 1;
                nmoves++;  
            }
            else{
                
                board->ghosts[board->n_ghosts-1].moves[nmovesmonster].command = 'W';
                board->ghosts[board->n_ghosts-1].moves[nmovesmonster].turns = 1;
                nmovesmonster++;
            }
            token = strtok(NULL, "\n");
        }
        
        else if(token[0] == 'A'){
        
            if(ispacman){
                 
                board->pacmans[0].moves[nmoves].command = 'A';
                board->pacmans[0].moves[nmoves].turns = 1;
                nmoves++;
            }
            
            else{
                board->ghosts[board->n_ghosts-1].moves[nmovesmonster].command = 'A';
                board->ghosts[board->n_ghosts-1].moves[nmovesmonster].turns = 1;
                nmovesmonster++;
            }
            token = strtok(NULL, "\n");
        }
        
        else if(token[0] == 'S'){
            
            
            if(ispacman){
                
                board->pacmans[0].moves[nmoves].command = 'S';
                board->pacmans[0].moves[nmoves].turns = 1;
                nmoves++;
            }
                
            else{
                   
                board->ghosts[board->n_ghosts-1].moves[nmovesmonster].command = 'S';
                board->ghosts[board->n_ghosts-1].moves[nmovesmonster].turns = 1;
                nmovesmonster++; 
            }   
            token = strtok(NULL, "\n");
        }
        
        else if(token[0] == 'D'){
    
            if(ispacman){
                   
                board->pacmans[0].moves[nmoves].command = 'D';
                board->pacmans[0].moves[nmoves].turns = 1;
                nmoves++; 
            }

            else{
                
                board->ghosts[board->n_ghosts-1].moves[nmovesmonster].command = 'D';
                board->ghosts[board->n_ghosts-1].moves[nmovesmonster].turns = 1;
                nmovesmonster++;
            }
            token = strtok(NULL, "\n");
        }
        
        else if(token[0] == 'R'){

            if(ispacman){
                    
                board->pacmans[0].moves[nmoves].command = 'R';
                board->pacmans[0].moves[nmoves].turns = 1;
                nmoves++;
            }
            
            else{
                
                board->ghosts[board->n_ghosts-1].moves[nmovesmonster].command = 'R';
                board->ghosts[board->n_ghosts-1].moves[nmovesmonster].turns = 1;
                nmovesmonster++;
            }
            token = strtok(NULL, "\n");
        }
        
        else if(token[0] == 'T'){
            sscanf(token,"T %d", &wait_turns);
            if(ispacman){
                  
                board->pacmans[0].moves[nmoves].command = 'T';
                board->pacmans[0].moves[nmoves].turns = wait_turns;
                board->pacmans[0].moves[nmoves].turns_left = wait_turns;
                nmoves++;  
            }

            else{
                board->ghosts[board->n_ghosts-1].moves[nmovesmonster].command = 'T';
                board->ghosts[board->n_ghosts-1].moves[nmovesmonster].turns = wait_turns;
                board->ghosts[board->n_ghosts-1].moves[nmovesmonster].turns_left = wait_turns;
                nmovesmonster++;
            }
            
            token = strtok(NULL, "\n");
        }
        
        else if(token[0] == 'C'){
            
            board->ghosts[board->n_ghosts-1].moves[nmovesmonster].command = 'C';
            board->ghosts[board->n_ghosts-1].moves[nmovesmonster].turns = 1;
            nmovesmonster++;
            token = strtok(NULL, "\n");
        }
        
    }
    if(ispacman){
        board->pacmans[0].pos_x = col;
        board->pacmans[0].pos_y = line;
        board->board[(line) * board->width + (col)].content = 'P'; // Pacman
        board->board[(line) * board->width + (col)].has_dot = 1;
        board->pacmans[0].alive = 1;
        board->pacmans[0].passo = wait_moves;
        board->pacmans[0].waiting = 0;
        board->pacmans[0].current_move = 0;
        board->pacmans[0].n_moves = nmoves;
        board->pacmans[0].points = accumulated_points;
        
    }
    else{  
        board->ghosts[board->n_ghosts-1].pos_x = col;
        board->ghosts[board->n_ghosts-1].pos_y = line;
        board->board[(line) * board->width + (col)].content = 'M'; // Monster
        board->ghosts[board->n_ghosts-1].passo = wait_moves;
        board->ghosts[board->n_ghosts-1].waiting = 0;   
        board->ghosts[board->n_ghosts-1].current_move = 0;
        board->ghosts[board->n_ghosts-1].n_moves = nmovesmonster; 
    }
    free(content);
    close(fd);
    return EXIT_SUCCESS;
}
