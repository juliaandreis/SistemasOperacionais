/*
gcc trabalhoGB.c -o mz -pthread
./mz -c entrada.txt compactado.mzp
./mz -d compactado.mzp saida.txt
diff -s entrada.txt saida.txt
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

// estrutura de estatísticas em memória compartilhada
typedef struct {
    unsigned long lidas, compactadas, nao_encontradas;
    unsigned long orig, comp;
    int done_c, done_d;
} stats_t;

// define o caractere de escape (ASCII 27)
#define ESC 27 

// Tabela de símbolos usados na compactação
static const unsigned char SYM[] = {
    // 1. Controle e Baixo ASCII (1 a 31, pulando 9, 10, 13 e 27)
    1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 
    28, 29, 30, 31, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
    50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70,
    71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91,
    92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110,
    111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126,
    128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143,
    144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
    160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175,
    176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,
    192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206,
};

static const char* WORD[] = {
    "de", "que", "para", "com", "nao", "por", "em", "uma", "o", "a", "as", "os", "ao", "e", "do", "da", "dos", "das", "no", "na",
    "academicos", "ajuda", "ainda", "aluno", "apenas", "aparecem", "aprendizado", "aqui", "arquivo", "arquivos", 
    "assim", "atividade", "ate", "avaliar", "bastante", "base", "cada", "caso", "combinacoes", "como", "compactacao", 
    "compressao", "comprova", "comum", "comuns", "concluir", "contem", "contextos", "continua", "criado", "criados", 
    "curtas", "demonstra", "demonstrar", "dentro", "dicionario", "diferenca", "diferentes", "direto", "duas", "efeito", 
    "eficiente", "ela", "ele", "encontra", "entender", "entre", "escrever", "esse", "este", "estes", "estimular", 
    "estruturas", "exemplo", "exibe", "facil", "fato", "faz", "fica", "foco", "foi", "formato", "frases", "frequentes", 
    "ganhar", "ganhos", "grava", "gravar", "ideia", "igual", "inclui", "isso", "juntos", "leitura", "ler", 
    "linha", "linhas", "logica", "longo", "maiores", "mais", "mas", "material", "maxima", "mesmo", "metodo", "menor", 
    "modo", "mostrar", "mostra", "muitas", "muito", "muitos", "normal", "nos", "nas", "objetivo", "observar", "ocorrencias", 
    "organizado", "original", "ou", "pai", "palavra", "palavras", "paragrafos", "pensamos", "pequeno", "pela", "pelo", 
    "pode", "pontuacao", "porque", "portugues", "possivel", "possui", "preparado", "preservar", "problema", "processo",  
    "processos", "programa", "quando", "reduz", "relatorio", "repetem", "repeticoes", "saida", "se", "sejam", "semelhantes", 
    "ser", "simples", "sobre", "solucao", "tamanho", "tambem", "tantas", "taxa", "tecnicos", "tem", "termos", "testes", 
    "texto", "textos", "threads", "trabalha", "trabalham", "trabalhar", "um", "usa", "uso", "util", "utiliza", "varias", 
    "varios", "ver", "vezes", "falamos", "escolhemos", "foque", "filho", "final", "estatisticas", "simbolo", "correspondente", 
    "encontre", "repetidas", "deve", "espacos", "descompactado", "seja", "exatamente", "conter", "c", "O", "repete", "Este", 
    "comprimir", "Assim", 
};


#define N_WORD (sizeof(WORD)/sizeof(WORD[0]))
#define N_SYM (sizeof(SYM)/sizeof(SYM[0]))

// função para verificar se um caractere precisa ser "protegido"
int precisa_escape(unsigned char c) {
    if (c == ESC) return 1;
    for (int i = 0; i < N_SYM; i++) {
        if (SYM[i] == c) return 1;
    }
    return 0;
}

int word_to_sym(const char *w){ for(int i=0;i<N_WORD;i++) if(!strcmp(w,WORD[i])) return SYM[i]; return -1; }
const char* sym_to_word(int c){ for(int i=0;i<N_WORD;i++) if(SYM[i]==c) return WORD[i]; return NULL; }

// memória compartilhada para estatísticas
stats_t *stats;

// buffers entre threads
char buf_lido[4096], buf_proc[4096];
int n_lido = 0, n_proc = 0;

pthread_mutex_t m_lido = PTHREAD_MUTEX_INITIALIZER, m_proc = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t c_lido = PTHREAD_COND_INITIALIZER, c_proc = PTHREAD_COND_INITIALIZER;

void* t_leitura(void *p){
    FILE *f = fopen((char*)p,"rb"); if(!f) exit(1);
    // lê no máximo 1 byte a menos para manter espaço para o \0 (para t_compactacao)
    size_t n = fread(buf_lido,1,sizeof(buf_lido)-1,f); 
    buf_lido[n] = 0; 

    pthread_mutex_lock(&m_lido); 
    n_lido = n; 
    pthread_cond_signal(&c_lido); 
    pthread_mutex_unlock(&m_lido);
    
    fclose(f); 
    return NULL;
}

void* t_compactacao(void *p){
    (void)p;
    int n_lido_local;

    pthread_mutex_lock(&m_lido); 
    while(n_lido == 0) pthread_cond_wait(&c_lido,&m_lido); 
    n_lido_local = n_lido; 
    pthread_mutex_unlock(&m_lido);
    
    char w[128]; int wl = 0; size_t cursor = 0;
    
    for(size_t i = 0; i < n_lido_local; i++){
        usleep(5000); // pausa visual para o monitor funcionar
        int c = (unsigned char)buf_lido[i];

        if(isalnum(c)){
            w[wl++] = c;
        } 
        else {
            if(wl){
                w[wl] = 0;
                wl = 0;
                stats->lidas++;

                int s = word_to_sym(w);
                if(s >= 0){
                    buf_proc[cursor++] = s;
                    stats->compactadas++;
                } else {
                    for(int j = 0; w[j]; j++){
                        if(precisa_escape((unsigned char)w[j])) 
                            buf_proc[cursor++] = ESC;
                        buf_proc[cursor++] = w[j];
                    }
                    stats->nao_encontradas++;
                }
            }
            if(precisa_escape(c)) buf_proc[cursor++] = ESC;
            buf_proc[cursor++] = c;
        }
    }
    
    if(wl){
        w[wl] = 0;
        stats->lidas++;

        int s = word_to_sym(w);
        if(s >= 0){
            buf_proc[cursor++] = s;
            stats->compactadas++;
        } else {
            for(int j = 0; w[j]; j++){
                if(precisa_escape((unsigned char)w[j])) 
                    buf_proc[cursor++] = ESC;
                buf_proc[cursor++] = w[j];
            }
            stats->nao_encontradas++;
        }
    }

    stats->orig = n_lido_local;
    stats->comp = cursor;

    pthread_mutex_lock(&m_proc);
    n_proc = cursor;
    pthread_cond_signal(&c_proc);
    pthread_mutex_unlock(&m_proc);

    return NULL;
}

void* t_gravacao(void *p){
    int n_grava_local;

    pthread_mutex_lock(&m_proc); 
    while(n_proc == 0) pthread_cond_wait(&c_proc,&m_proc); 
    n_grava_local = n_proc; 
    pthread_mutex_unlock(&m_proc);
    
    FILE *f = fopen((char*)p,"wb"); if(!f) exit(1);
    fwrite(buf_proc, 1, n_grava_local, f); 
    fclose(f);
    stats->done_c = 1; 
    return NULL;
}

void compactar(const char *in, const char *out){
    pthread_t a,b,c;
    pthread_create(&a,NULL,t_leitura,(void*)in);
    pthread_create(&b,NULL,t_compactacao,NULL);
    pthread_create(&c,NULL,t_gravacao,(void*)out);
    pthread_join(a,NULL); pthread_join(b,NULL); pthread_join(c,NULL);
}

void descompactar(const char *in, const char *out){
    FILE *fi = fopen(in,"rb");
    FILE *fo = fopen(out,"wb");
    if(!fi || !fo) { perror("Erro arquivos"); exit(1); }
    
    int c;
    stats->lidas = stats->compactadas = stats->nao_encontradas = stats->orig = stats->comp = 0; // reset
    
    while((c = fgetc(fi)) != EOF){
        usleep(5000); 
        stats->lidas++; 
        stats->orig++; // contabiliza tamanho do arquivo de entrada (compactado)

        if (c == ESC) {
            int proximo = fgetc(fi);
            if (proximo != EOF){
                fputc(proximo, fo);
                stats->lidas++; // leu mais um byte
                stats->orig++;
                stats->compactadas++; 
                stats->comp++; // escreveu 1 byte no arquivo final
            }
        } 
        else {
            const char* w = sym_to_word(c);
            if(w){
                fputs(w, fo); 
                stats->compactadas++; 
                stats->comp += strlen(w); // escreveu strlen(w) bytes no arquivo final
            } 
            else {
                fputc(c, fo);
                stats->compactadas++;
                stats->comp++; // escreveu 1 byte no arquivo final
            }
        }
    }
    fclose(fi); fclose(fo);
    stats->done_d = 1; // sinaliza que o descompactador terminou
}


void monitorar(char mode){
    printf("[Monitor] Iniciado. Aguardando processos...\n");

    if(mode == 'c') {
        if(mode == 'c') {
        // MONITORANDO COMPACTAÇÃO
        while(!stats->done_c){
            printf("\r[Monitor] Lidas: %lu | Compactadas: %lu | Nao encontradas: %lu     ",
                stats->lidas, stats->compactadas, stats->nao_encontradas);
            fflush(stdout);
            usleep(200000);
        }
        printf("\r[Monitor] Lidas: %lu | Compactadas: %lu | Nao encontradas: %lu\n",
                stats->lidas, stats->compactadas, stats->nao_encontradas);
        printf("\n[Monitor] Processo de compactacao finalizado.\n");

        if(stats->orig > 0){
            double p = 100.0 * (1.0 - ((double)stats->comp / (double)stats->orig));
            printf("\n[Monitor] ------ RELATORIO COMPACTAÇÃO ------\n\n"); 
            printf("          Total Lidas: %lu\n", stats->lidas);      
            printf("          Total Compactadas: %lu \n", stats->compactadas);
            printf("          Total Nao encontradas: %lu\n", stats->nao_encontradas);
            printf("          Tamanho Original (entrada)= %lu B\n", stats->orig);
            printf("          Tamanho Compactado (saida)= %lu B\n",stats->comp);
            printf("          Compressao: %.2f%%\n", p);
            printf("--------------------------------------------------\n");
        }
        
        // pausa para o usuário ler antes de começar a validação
        sleep(3);
        
        // MONITORANDO COMPACTAÇÃO TESTE
        // espera o descompactador terminar
        while(!stats->done_d){
             printf("\r[Monitor] Bytes Lidos: %lu | Tokens Processados: %lu     ",
                    stats->lidas, stats->compactadas);
             fflush(stdout);
             usleep(200000);
        }
        
        printf("\r[Monitor] Bytes Lidos: %lu | Tokens Processados: %lu\n",
                    stats->lidas, stats->compactadas);
        printf("\n[Monitor] Processo de descompactacao (teste) finalizado.\n");
        printf("\n[Monitor] ------ RELATORIO DESCOMPACTAÇÃO ------\n\n");
        printf("          Total Tokens Processados: %lu\n", stats->compactadas);
        printf("          Tamanho Arquivo Compactado (entrada) = %lu B\n", stats->orig);
        printf("          Tamanho Arquivo Descompactado (saida) = %lu B\n",stats->comp);
        printf("--------------------------------------------------\n");

    }
    } else if (mode == 'd') {
        // modo descompactação
        while(!stats->done_d){
            printf("\r[Monitor] Bytes Lidos: %lu | Tokens Processados: %lu",
                    stats->lidas, stats->compactadas);
            fflush(stdout);
            usleep(200000);
        }
        printf("\r[Monitor] Bytes Lidos: %lu | Tokens Processados: %lu\n",
                    stats->lidas, stats->compactadas);
        printf("[Monitor] Processo finalizado.\n");
        printf("\n[Monitor] Total Tokens Processados: %lu\n", stats->compactadas);
        printf("          Tamanho Arquivo Compactado (entrada) = %lu B\n", stats->orig);
        printf("          Tamanho Arquivo Descompactado (saida) = %lu B\n",stats->comp);
    }
}


int main(int argc, char **argv){
    if(argc != 4){ printf("uso: %s -c|-d [arq_entrada] [arq_saida]\n",argv[0]); return 1; }

    // validação do dicionário
    if(N_WORD != N_SYM){
        printf("Erro: O numero de PALAVRAS (%lu) e SIMBOLOS (%lu) nao e igual!\n", N_WORD, N_SYM);
        return 1;
    }

    stats = mmap(NULL,sizeof(stats_t),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
    memset(stats,0,sizeof(stats_t));
    
    char mode = argv[1][1]; 

    // 3 processos
    pid_t c1 = fork(); // processo 1: compactador
    if(c1 == 0){
        if(mode == 'c') {
            printf("\n[Processo 1] Iniciando compactacao...\n"); 
            compactar(argv[2],argv[3]);
        }
        exit(0);
    }

    pid_t c2 = fork(); // processo 2: descompactador
    if(c2 == 0){
        if(mode == 'c'){
            // espera o compactador terminar
            while(!stats->done_c) { 
                usleep(100000); 
            }
            // espera 2 segundos para o monitor imprimir o relatório
            sleep(2); 
            printf("\n[Processo 2] Compactacao detectada. Iniciando descompactacao de teste...\n");
            descompactar(argv[3],"saida_teste.txt");
            
        } else if (mode == 'd') {
            descompactar(argv[2],argv[3]);
        }
        exit(0);
    }
    
    monitorar(mode); // processo 3: monitor 
    
    wait(NULL); 
    wait(NULL);
    
    munmap(stats, sizeof(stats_t)); 
    return 0;
}