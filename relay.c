/*
relay.c - basic relay control utility for FT245RL boards
can arbitrary open/close/toggle relays
supports hex, dec and character pins

building:
gcc -Wall -Os -lftdi relay.c -o relay && strip relay

todo:
warn on overlapping commands
handle more errors
some way of dealing with multiple boards
verbose mode
named pipe support
properly handle ^C
*/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <termios.h>
#include <wordexp.h>
#include <ftdi.h>

enum format_type { HEX, DEC, INT, ONE, RAW };
enum input_type { ARGS, CHAR, LINE };
enum operation_type { SET, CLOSE, OPEN, TOGGLE };
char* int_order = "01234567";
char* one_order = "12345678";
struct termios old_tio, new_tio;

void help(void)
{
    printf(""
        "relay - basic usb relay control utility\n"
        "    made for the Sainsmart 8 channel board\n"
        "    but should work on anything with a FT245RL\n"
        "\nbit-wise commands:\n"
        "    multiple commands can be used in the same call\n"
        "    -c XX (close)\n"
        "    -o XX (open)\n"
        "    -s XX (set all) given bits are closed, non-given are opened\n"
        "    -t XX (toggle)\n"
        "    -r (read) outputs pin values to stdout\n"
        "\nformat options:\n"
        "    in decreasing order of leetness\n"
        "    -R (raw) raw binary byte IO\n"
        "    -X (hex) hex byte IO (default)\n"
        "    -D (decimal) decimal byte IO\n"
        "    -I (integer) 0-7 character IO\n"
        "    -1 (one-indexed) 1-8 character IO\n"
        "\nstreaming options:\n"
        "    monitors stdin for fewer calls and less overhead\n"
        "    format cannot be changed after entering a stream\n"
        "    -C (character stream) works with RI1 formats\n"
        "    -L (line stream) works with XDI1 formats\n"
        "\nmisc:\n"
        "    -h (help) this text\n"
        "\nbugs:\n"
        "    no error handling or warnings!\n"
        "    confused by multiple boards\n"
        "    character streaming might be non-portable\n"
        "\n");
}

void config_stdin_char(void)
{
    // technique from http://shtrom.ssji.net/skb/getc.html
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &=(~ICANON & ~ECHO);
    tcsetattr(STDIN_FILENO,TCSANOW,&new_tio);
}

int switch_format(enum format_type f, int arg)
{
    switch (arg)
    {
        case 'X': return HEX; break;
        case 'D': return DEC; break;
        case 'I': return INT; break;
        case '1': return ONE; break;
        case 'R': return RAW; break;
        default: return f; break;
    }
    return f;
}

int switch_operation(enum operation_type op, int arg)
{
    switch (arg)
    {
        case 'c': return CLOSE; break;
        case 'o': return OPEN; break;
        case 's': return SET; break;
        case 't': return TOGGLE; break;
        default: return op; break;
    }
    return op;
}

int parse_chars(char* order, char* s)
// "list" of integers to byte
{
    int b = 0;
    int i;
    for (i=0; i<strlen(order); i++)
    {
        if (strchr(s, order[i]) == NULL)
            {continue;}
        b += (1<<i);
    }
    return b;
}

int parse_bits(enum format_type f, char* s)
// returns value 0-255
{
    switch (f)
    {
        case HEX:
            return (int)strtol(s, NULL, 16); break;
        case DEC:
            return (int)strtol(s, NULL, 10); break;
        case INT:
            return parse_chars(int_order, s); break;
        case ONE:
            return parse_chars(one_order, s); break;
        case RAW:
            return (int)(s[0]); break;
    }
    return -1;
}

void show_chars(char* order, int n)
// byte to "list" of integers on stdout
{
    int i;
    for (i=0; i<strlen(order); i++)
    {
        if (n & (1<<i))
            {printf("%c", order[i]);}
    }
    printf("\n");
}

void show(enum format_type f, int n)
// prints to stdout
{
    switch (f)
    {
        case HEX:
            printf("0x%02x\n", n); break;
        case DEC:
            printf("%i\n", n); break;
        case INT:
            show_chars(int_order, n); break;
        case ONE:
            show_chars(one_order, n); break;
        case RAW:
            printf("%c", (char)n); break;
    }
}

int do_input(int state, enum operation_type op, enum format_type f, char* s)
// returns new value of state
{
    int pins = parse_bits(f, s);
    switch (op)
    {
        case CLOSE:
            state |= pins; break;
        case OPEN:
            state &= ~pins; break;
        case SET:
            state = pins; break;
        case TOGGLE:
            state ^= pins; break;
        default: break;
    }
    return state;
}

void remove_newline(char* line_str, int line_len)
{
    int i;
    for (i=0; i<line_len; i++)
    {
        if (line_str[i] == '\0')
            {break;}
        if (line_str[i] == '\n')
            {line_str[i] = ' ';}
    }
}

int main(int argc, char* argv[])
{
    uint8_t c = 0;
    uint8_t c2 = 0;
    struct ftdi_context ftdic;
    int r, opt, state;
    enum format_type format = HEX;
    enum operation_type operation = SET;
    enum input_type input = ARGS;
    char mini_string[] = ".";  // for char->str conversion
    size_t line_len;
    char* line_str1 = NULL;
    char* line_str2 = NULL;
    char* prefix = "arg0 ";
    wordexp_t l_args;

    // pass 1: sanity check
    // parse_bits() is a no-op here, reserved for error checking
    r = 1;
    while ((opt = getopt(argc, argv, "XDI1RCLhrc:o:s:t:")) != -1)
    {
        switch (opt)
        {
            case 'X': format = HEX; break;
            case 'D': format = DEC; break;
            case 'I': format = INT; break;
            case '1': format = ONE; break;
            case 'R': format = RAW; break;
            case 'C':
                input = CHAR;
                r = 0;
                break;
            case 'L':
                input = LINE;
                r = 0;
                break;
            case 'c':
            case 'o':
            case 's':
            case 't':
                parse_bits(format, optarg);
            case 'r':
                r = 0;
                break;
            case 'h':
                help();
                return 0;
                break;
            default:
                help();
                return 1;
                break;
        }
    }
    if (r)  // no commands were given
    {
        help();
        return 1;
    }

    // set up driver context
    ftdi_init(&ftdic);

    // todo, fix hard coded vid/pid
    if(ftdi_usb_open(&ftdic, 0x0403, 0x6001) < 0)
    {
        fprintf(stderr, "no device\n");
        return 1;
    }

    // todo, proper masking for smaller boards
    ftdi_set_bitmode(&ftdic, 0xFF, BITMODE_BITBANG);

    format = HEX;

    // store initial state for later toggles
    r = ftdi_read_pins(&ftdic, &c);
    state = (int)c;

    if (input == CHAR)
        {config_stdin_char();}

    // reset getopt
    optind = 1;
    #ifdef BSD
    optreset = 1;
    #endif
    // pass 2: commands
    while ((opt = getopt(argc, argv, "XDI1RCLhrc:o:s:t:")) != -1)
    {
        format = switch_format(format, opt);
        operation = switch_operation(operation, opt);
        switch (opt)
        {
            case 'c':
            case 'o':
            case 's':
            case 't':
                state = do_input(state, operation, format, optarg);
                break;
            case 'r':
                show(format, state);
                break;
            default:
                break;
        }
    }

    if (state < 0) {};
    if (state > 0xFF) {};
    c = (uint8_t)state;
    ftdi_write_data(&ftdic, &c, 1);

    // char stream input loop
    while (input == CHAR)
    {
        c = getchar();
        if ((format == RAW) | (strchr("costr", c) == NULL))
        {
            mini_string[0] = c;
            state = do_input(state, operation, format, mini_string);
            c2 = (uint8_t)state;
            ftdi_write_data(&ftdic, &c2, 1);
            continue;
        }
        
        operation = switch_operation(operation, c);
        if (c == 'r')
            {show(format, state);}
    }

    // line stream input loop
    while (input == LINE)
    {
        if (getline(&line_str1, &line_len, stdin) == -1)
            {break;}
        // prep into something wordexp can handle
        remove_newline(line_str1, line_len);
        line_str2 = malloc(strlen(prefix) + strlen(line_str1) + 1);
        strcpy(line_str2, prefix);
        strcat(line_str2, line_str1);
        free(line_str1);
        // crunch input into args for getopt
        r = wordexp(line_str2, &l_args, 0);
        if (r == WRDE_NOSPACE)
            {wordfree(&l_args);}
        if (r != 0)
            {free(line_str2); continue;}

        // reset getopt
        optind = 1;
        #ifdef BSD
        optreset = 1;
        #endif
        // exact same code as normal arg handling
        while ((opt = getopt(l_args.we_wordc, l_args.we_wordv, "XDI1RCLhrc:o:s:t:")) != -1)
        {
            format = switch_format(format, opt);
            operation = switch_operation(operation, opt);
            switch (opt)
            {
                case 'c':
                case 'o':
                case 's':
                case 't':
                    state = do_input(state, operation, format, optarg);
                    break;
                case 'r':
                    show(format, state);
                    break;
                default:
                    break;
            }
        }

        if (state < 0) {};
        if (state > 0xFF) {};
        c = (uint8_t)state;
        ftdi_write_data(&ftdic, &c, 1);

        wordfree(&l_args);
        free(line_str2);
    }

    // clean up
    ftdi_usb_close(&ftdic);
    //ftdi_free(&ftdic);
    if (input == CHAR)
        {tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);}
    return 0;
}

