#include <stdbool.h>
#include <stdio.h>
#include "usb_serial.h"

#define BUFFER_SIZE 64

typedef enum {
    PARITY_EVEN,
    PARITY_ODD
} ParityState_t;

typedef enum {
    REM_R0,
    REM_R1,
    REM_R2
} RemainderState_t;

typedef enum {
    PAT_S0,
    PAT_S1,
    PAT_S2,
    PAT_S3
} PatternState_t;

typedef enum {
    DEC_START,
    DEC_SIGN,
    DEC_INT_DIGITS,
    DEC_DOT_SEEN,
    DEC_FRAC_DIGITS,
    DEC_INVALID
} DecimalState_t;

static inline bool isDecimalDigit(char c) {
    return (c >= '0' && c <= '9');
}

static void handleParityMachine(const char *input) {
    bool hasDigit = false;
    bool invalid = false;
    int lastDigit = 0;

    for (size_t i = 0; input[i] != '\0'; ++i) {
        const char c = input[i];
        if (!isDecimalDigit(c)) {
            invalid = true;
            break;
        }
        hasDigit = true;
        lastDigit = c - '0';
    }

    printf("\n[a] Maquina de paridade\n");

    if (!hasDigit || invalid) {
        printf("Entrada invalida. Informe apenas um inteiro decimal nao negativo.\n");
        return;
    }

    if ((lastDigit % 2) == 0) {
        printf("Resultado: par\n");
    } else {
        printf("Resultado: impar\n");
    }
}

static void handleDivisibleByThreeMachine(const char *input) {
    RemainderState_t state = REM_R0;
    bool hasBit = false;
    bool invalid = false;

    for (size_t i = 0; input[i] != '\0'; ++i) {
        const char c = input[i];
        if (c != '0' && c != '1') {
            invalid = true;
            break;
        }

        hasBit = true;

        switch (state) {
            case REM_R0:
                state = (c == '0') ? REM_R0 : REM_R1;
                break;
            case REM_R1:
                state = (c == '0') ? REM_R2 : REM_R0;
                break;
            case REM_R2:
                state = (c == '0') ? REM_R1 : REM_R2;
                break;
        }
    }

    printf("\n[b] Maquina de divisibilidade por 3\n");

    if (!hasBit || invalid) {
        printf("Entrada invalida. Use apenas bits 0 e 1.\n");
        return;
    }

    if (state == REM_R0) {
        printf("Resultado: divisivel por 3\n");
    } else {
        printf("Resultado: nao divisivel por 3\n");
    }
}

static void handlePattern1011Machine(const char *input) {
    PatternState_t state = PAT_S0;
    bool hasBit = false;
    bool invalid = false;
    size_t occurrences = 0;

    printf("\n[c] Maquina de deteccao de 1011\n");

    for (size_t i = 0; input[i] != '\0'; ++i) {
        const char c = input[i];
        if (c != '0' && c != '1') {
            invalid = true;
            break;
        }

        hasBit = true;

        switch (state) {
            case PAT_S0:
                state = (c == '1') ? PAT_S1 : PAT_S0;
                break;
            case PAT_S1:
                state = (c == '0') ? PAT_S2 : PAT_S1;
                break;
            case PAT_S2:
                state = (c == '1') ? PAT_S3 : PAT_S0;
                break;
            case PAT_S3:
                if (c == '1') {
                    ++occurrences;
                    printf("Ocorrencia ");
                    printf("%lu", (uint32_t)occurrences);
                    printf(" detectada na posicao ");
                    printf("%lu", (uint32_t)(i + 1));
                    printf("\n");
                    state = PAT_S1;
                } else {
                    state = PAT_S2;
                }
                break;
        }
    }

    if (!hasBit || invalid) {
        printf("Entrada invalida. Use apenas bits 0 e 1.\n");
        return;
    }

    if (occurrences == 0) {
        printf("Nenhuma ocorrencia de 1011 foi encontrada.\n");
    } else {
        printf("Total de ocorrencias: ");
        printf("%lu", (uint32_t)occurrences);
        printf("\n");
    }
}

static void handleDecimalValidationMachine(const char *input) {
    DecimalState_t state = DEC_START;
    bool hasIntegerDigit = false;
    bool hasFractionDigit = false;
    bool invalid = false;

    printf("\n[d] Maquina de validacao decimal\n");

    for (size_t i = 0; input[i] != '\0'; ++i) {
        const char c = input[i];

        if (c == ' ' || c == '\t') {
            invalid = true;
            state = DEC_INVALID;
            break;
        }

        switch (state) {
            case DEC_START:
                if (c == '+' || c == '-') {
                    state = DEC_SIGN;
                } else if (isDecimalDigit(c)) {
                    state = DEC_INT_DIGITS;
                    hasIntegerDigit = true;
                } else {
                    invalid = true;
                    state = DEC_INVALID;
                }
                break;

            case DEC_SIGN:
                if (isDecimalDigit(c)) {
                    state = DEC_INT_DIGITS;
                    hasIntegerDigit = true;
                } else {
                    invalid = true;
                    state = DEC_INVALID;
                }
                break;

            case DEC_INT_DIGITS:
                if (isDecimalDigit(c)) {
                    hasIntegerDigit = true;
                } else if (c == '.') {
                    state = DEC_DOT_SEEN;
                } else {
                    invalid = true;
                    state = DEC_INVALID;
                }
                break;

            case DEC_DOT_SEEN:
                if (isDecimalDigit(c)) {
                    state = DEC_FRAC_DIGITS;
                    hasFractionDigit = true;
                } else {
                    invalid = true;
                    state = DEC_INVALID;
                }
                break;

            case DEC_FRAC_DIGITS:
                if (isDecimalDigit(c)) {
                    hasFractionDigit = true;
                } else {
                    invalid = true;
                    state = DEC_INVALID;
                }
                break;

            case DEC_INVALID:
                invalid = true;
                break;
        }

        if (invalid) {
            break;
        }
    }

    if (!invalid && state == DEC_DOT_SEEN) {
        invalid = true;
    }

    if (!invalid && !hasIntegerDigit) {
        invalid = true;
    }

    if (!invalid && state == DEC_FRAC_DIGITS && !hasFractionDigit) {
        invalid = true;
    }

    if (invalid) {
        printf("Resultado: invalido\n");
    } else {
        printf("Resultado: valido\n");
    }
}

static void print_menu(void) {
    printf("\n=== Atividade 2 - Maquinas de Estados ===\n");
    printf("1 - Par ou impar (inteiro decimal)\n");
    printf("2 - Divisivel por 3 (bits do MSB ao LSB)\n");
    printf("3 - Detectar sequencia 1011 em fluxo de bits\n");
    printf("4 - Validar numero decimal\n");
    printf("Escolha uma opcao (1-4): ");
    fflush(stdout);
}

static void read_line(char *input, size_t maxSize)
{
    size_t idx = 0;

    while (1) {
        int ch = usb_serial_getchar();
        if (ch < 0) {
            continue;
        }
        if ((ch == '\r' || ch == '\n') && idx == 0) {
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            break;
        }

        if (ch == '\b' || ch == 127) {
            if (idx > 0) {
                idx--;
            }
            continue;
        }
        if (ch >= 32 && ch <= 126) {
            if (idx < (maxSize - 1)) {
                input[idx++] = (char)ch;
            }
        }
    }

    input[idx] = '\0';
}

int main(void) {
    char input[BUFFER_SIZE];
    bool show_menu = true;
    usb_serial_init();
    printf("\r\n");
    print_menu();
    show_menu = false;

    while (1) {
        if (show_menu) {
            print_menu();
            show_menu = false;
        }
        read_line(input, sizeof(input));

        switch (input[0]) {
            case '1':
                printf("\nDigite um inteiro decimal nao negativo: ");
                fflush(stdout);
                read_line(input, sizeof(input));
                handleParityMachine(input);
                break;

            case '2':
                printf("\nDigite os bits (MSB ao LSB): ");
                fflush(stdout);
                read_line(input, sizeof(input));
                handleDivisibleByThreeMachine(input);
                break;

            case '3':
                printf("\nDigite o fluxo de bits para deteccao de 1011: ");
                fflush(stdout);
                read_line(input, sizeof(input));
                handlePattern1011Machine(input);
                break;

            case '4':
                printf("\nDigite o numero decimal para validacao: ");
                fflush(stdout);
                read_line(input, sizeof(input));
                handleDecimalValidationMachine(input);
                break;
            
            case 'm':
            case 'M':
                show_menu = 1;
                break;
            default:
                printf("\nOpcao invalida. Digite 1, 2, 3 ou 4.\n");
                fflush(stdout);
                break;
        }

        printf("\nDigite 'm' para ver o menu novamente.\n");
        fflush(stdout);
    }
}