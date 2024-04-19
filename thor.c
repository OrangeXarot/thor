/*** INCLUDES ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** DEFINES ***/

#define THOR_VERSION "0.2.0"
#define THOR_TAB_STOP 8
#define THOR_QUIT_TIMES 3

#define CTRL_KEY(k) ((k) & 0x1f) 

enum editorKey {
    SCROLL_UP = 25,
    SCROLL_DOWN = 5,
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    S_ARROW_LEFT,
    S_ARROW_DOWN,
    S_ARROW_RIGHT,
    S_ARROW_UP,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

enum editorModes {
    COMMAND = 500,
    INSERT    
};

enum editorHighlight {
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_MLCOMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)


/*** DATA ***/

struct editorSyntax {
    char *filetype;
    char **filematch;
    char **keywords;
    char *singleline_comment_start;
    char *multiline_comment_start;
    char *multiline_comment_end;
    int flags;
};

typedef struct erow {
    int idx;
    int size;
    int rsize;
    char *chars;
    char *render;
    unsigned char *hl;
    int hl_open_comment;
} erow;

typedef struct copyrow {
    int lines;
    int size;
    char *chars;
} copyrow;

struct editorConfig {
    int cx, cy;
    int rx;
    int ox;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    copyrow *cprow;
    int mode;
    int dirty;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct editorSyntax *syntax;
    struct termios orig_termios;
};

struct editorConfig E;

/*** FILETYPES ***/

char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL };
char *C_HL_keywords[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else", 
    "struct", "union", "typedef", "enum", "class", "case",

    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|", 
    "void|", "#define|", "#include|", "NULL|", NULL
};

char *SHELL_HL_extensions[] = { ".sh", NULL};
char *SHELL_HL_keywords[] = {
    "if", "fi", "read", "echo", "for", "while", "do", "done", "elif", "else", NULL
};

char *TXT_HL_extensions[] = { ".txt", NULL };
char *TXT_HL_keywords[] = { NULL };


struct editorSyntax HLDB[] = {
    {
        "C",
        C_HL_extensions,
        C_HL_keywords,
        "//", "/*", "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    },
    {
        "SHELL",
        SHELL_HL_extensions,
        SHELL_HL_keywords,
        "#", "/*", "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    },
    {
        "TEXT FILE",
        TXT_HL_extensions,
        TXT_HL_keywords,
        "", "", "",
        HL_HIGHLIGHT_NUMBERS 
    }
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/*** PROTOTYPES ***/

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));
void editorMoveCursor(int key);
void editorDelRow(int at);

/*** TERMINAL ***/

void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode() {
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");
    atexit(disableRawMode);


    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8); 
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

int editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if(nread == -1 && errno != EAGAIN) die("read");
    }

    
    if(c == '\x1b') {
        char seq[3];

        if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

                  
        if(seq[0] == '[') {
            if(seq[1] >= '0' && seq[1] <= '9') {
                if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if(seq[2] == '~') {
                    switch(seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch(seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                    case 'P': return DEL_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch(seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';
    } else {
        return c;
    }

}

int getWindowSize(int *rows, int *cols) { 
    struct winsize ws;

    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return -1;
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** SYNTAX HIGHLIGHTING ***/

int is_separator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void editorUpdateSyntax(erow *row) {
    row->hl = realloc(row->hl, row->rsize);
    memset(row->hl, HL_NORMAL, row->rsize);

    if(E.syntax == NULL) return;

    char **keywords = E.syntax->keywords;

    char *scs = E.syntax->singleline_comment_start;
    char *mcs = E.syntax->multiline_comment_start;
    char *mce = E.syntax->multiline_comment_end;

    int scs_len = scs ? strlen(scs) : 0;
    int mcs_len = mcs ? strlen(mcs) : 0;
    int mce_len = mce ? strlen(mce) : 0;

    int prev_sep = 1;
    int in_string = 0;
    int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

    int i = 0;
    while(i < row->rsize) {
        char c = row->render[i];
        unsigned char prev_hl = (i > 0) ? row->hl [i-1] : HL_NORMAL;

        if(scs_len && !in_string && !in_comment) {
            if(!strncmp(&row->render[i], scs, scs_len)) {
                memset(&row->hl[i], HL_COMMENT, row->rsize - i);
                break;
            }
        }

        if(mcs_len && mce_len && !in_string) {
            if(in_comment) {
                row->hl[i] = HL_MLCOMMENT;
                if(!strncmp(&row->render[i], mce, mce_len)) {
                    memset(&row->hl[i], HL_MLCOMMENT, mce_len);
                    i += mce_len;
                    in_comment = 0;
                    prev_sep = 1;
                    continue;
                } else {
                    i++;
                    continue;
                }
            } else if(!strncmp(&row->render[i], mcs, mcs_len)) {
                memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
                i += mcs_len;
                in_comment = 1;
                continue;
            }
        }

        if(E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            if(in_string) {
                row->hl[i] = HL_STRING;
                if(c == '\\' &&  i + 1 < row->rsize) {
                    row->hl[i + 1] = HL_STRING;
                    i+= 2;
                    continue;
                }
                if(c == in_string) in_string = 0;
                i++;
                prev_sep = 1;
                continue;
            } else {
                if(c == '"' || c == '\'' || c == '`') {
                    in_string = c;
                    row->hl[i] = HL_STRING;
                    i++;
                    continue;
                }
            }
        }


        if(E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            if((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || 
                    (c == '.' && prev_hl == HL_NUMBER)) {
                row->hl[i] = HL_NUMBER;
                i++;
                prev_sep = 0;
                continue;
            }
        }

        if(prev_sep) {
            int j;
            for(j = 0; keywords[j]; j++) {
                int klen = strlen(keywords[j]);
                int kw2 = keywords[j][klen - 1] == '|';
                if(kw2) klen--;

                if(!strncmp(&row->render[i], keywords[j], klen) &&
                        is_separator(row->render[i + klen])) {
                    memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
                    i += klen;
                    break;
                }
            }
            if(keywords[j] != NULL) {
                prev_sep = 0;
                continue;
            }
        }

        prev_sep = is_separator(c);
        i++;
    }

    int changed = (row->hl_open_comment != in_comment);
    row->hl_open_comment = in_comment;
    if(changed && row->idx + 1 < E.numrows)
        editorUpdateSyntax(&E.row[row->idx + 1]); 
}


int editorSyntaxToColor(int hl) {
    switch(hl) {
        case HL_COMMENT: 
        case HL_MLCOMMENT: return 96;
        case HL_KEYWORD1: return 93;
        case HL_KEYWORD2: return 92;
        case HL_STRING: return 95;
        case HL_NUMBER: return 91;
        case HL_MATCH: return 43;
        default: return 37;
    }
}

void editorSelectSyntaxHighlight() {
    E.syntax = NULL;
    if(E.filename == NULL) return;

    char *ext = strrchr(E.filename, '.');

    for(unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        struct editorSyntax *s = &HLDB[j];
        unsigned int i = 0;
        while(s->filematch[i]) {
            int is_ext = (s->filematch[i][0] == '.');
            if((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
                    (!is_ext && strstr(E.filename, s->filematch[i]))) {
                E.syntax = s;

                int filerow;
                for(filerow = 0; filerow < E.numrows; filerow++) {
                    editorUpdateSyntax(&E.row[filerow]);
                }
                return;
            }
            i++;
        }
    }
}

/*** ROW OPERATIONS ***/

int editorRowCxToRx(erow *row, int cx) {
    int rx = 0;
    int j;
    for(j = 0; j < cx; j++) {
        if(row->chars[j] == '\t')
            rx += (THOR_TAB_STOP - 1) - (rx % THOR_TAB_STOP);
        rx++;
    }
    return rx;
}

int editorRowRxToCx(erow *row, int rx) {
    int cur_rx = 0;
    int cx;
    for(cx = 0; cx < row->size; cx++) {
        if(row->chars[cx] == '\t')
            cur_rx += (THOR_TAB_STOP - 1) - (cur_rx % THOR_TAB_STOP);
        cur_rx++;

        if(cur_rx > rx) return cx;
    }
    return cx;
}

void editorUpdateRow(erow *row) {
    int tabs = 0;
    int j;
    for(j = 0; j < row->size; j++)
        if(row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs*(THOR_TAB_STOP - 1) + 1);

    int idx = 0;
    for(j = 0; j < row->size; j++) {
        if(row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while(idx % 8 != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;

    editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) {
    if (at < 0 || at > E.numrows) return;

    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
    for(int j = at + 1; j <= E.numrows; j++) E.row[j].idx++;

    E.row[at].idx = at;

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    E.row[at].hl = NULL;
    E.row[at].hl_open_comment = 0;
    editorUpdateRow(&E.row[at]);

    E.numrows++;
    E.dirty++;
}

void editorYankRow(int at, int lines) {
    if (E.cprow) {
        for (int i = 0; i < E.cprow[0].lines; i++) {
            free(E.cprow[i].chars);
        }
        free(E.cprow);
        E.cprow = NULL;
    }

    E.cprow = malloc(sizeof(copyrow) * lines);

    for(int i = 0; i < lines; i++) {
        E.cprow[i].size = E.row[at + i].size;
        E.cprow[i].chars = malloc(E.row[at + i].size);
        memcpy(E.cprow[i].chars, E.row[at + i].chars, E.row[at + i].size);
    }
    E.cprow[0].lines = lines;
    editorSetStatusMessage("Yanked %d lines", lines);
}

void editorDelYankRow(int at, int lines) {
    editorYankRow(at, lines);

    for(int i = 0; i < lines; i++) {
        editorDelRow(at);
    }  
    editorSetStatusMessage("Deleted %d lines", lines);
}

void editorPasteRows() {
    if(E.cprow == NULL) editorSetStatusMessage("Nothing in Yank Buffer");
    else {
        for(int i = 0; i < E.cprow[0].lines; i++) {
            editorInsertRow(E.cy + 1 + i, E.cprow[i].chars, E.cprow[i].size);
        }
        for(int i = 0; i < E.cprow[0].lines; i++) {
            editorMoveCursor(ARROW_DOWN);
        }
        editorSetStatusMessage("Pasted %d lines", E.cprow[0].lines);
    }
}

void editorFreeRow(erow *row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
}

void editorDelRow(int at) {
    if(at < 0 || at >= E.numrows) return;
    editorFreeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
    for(int j = at; j < E.numrows - 1; j++) E.row[j].idx--;
    E.numrows--;
    E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
    if(at < 0 || at > row->size) at = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
    if(at < 0 || at >= row->size) return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}

/*** EDITOR OPERATIONS ***/

void editorInsertChar(int c) {
    if(E.cy == E.numrows) {
        editorInsertRow(E.numrows, "", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

void editorInsertNewline() {
    if(E.cx == 0) {
        editorInsertRow(E.cy, "", 0);
    } else {
        erow *row = &E.row[E.cy];
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }
    E.cy++;
    E.cx = 0;
}

void editorDelChar() {
    if(E.cy == E.numrows) return;
    if(E.cx == 0 && E.cy == 0) return;

    erow *row = &E.row[E.cy];
    if(E.cx > 0) {
        editorRowDelChar(row, E.cx - 1);
        E.cx--;
    } else {
        E.cx = E.row[E.cy - 1].size;
        editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
        editorDelRow(E.cy);
        E.cy--;
    }
}


/*** FILE I/O ***/

char *editorRowsToString(int *buflen) {
    int totlen = 0;
    int j;
    for(j = 0; j < E.numrows; j++)
        totlen += E.row[j].size + 1;
    *buflen = totlen;

    char *buf = malloc(totlen);
    char *p = buf;
    for(j = 0; j < E.numrows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }

    return buf;
}

int numPlaces(int n) {
    int r = 1;
    if(n < 0) n = (n == INT_MIN) ? INT_MAX : -n;
    while(n > 9) {
        n /= 10;
        r++;
    }
    return r;
}

void editorOpen(char *filename) {
    free(E.filename);
    E.filename = strdup(filename);

    editorSelectSyntaxHighlight();

    FILE *fp = fopen(filename, "r");
    if(!fp) die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while(linelen > 0 && (line[linelen - 1] == '\n' ||
                    line[linelen - 1] == '\r'))
            linelen--;
        editorInsertRow(E.numrows, line, linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
}

void editorSave() {
    if(E.filename == NULL) {
        E.filename = editorPrompt("Save as: %s", NULL);
        if(E.filename == NULL) {
            editorSetStatusMessage("Save Aborted");
            return;
        }
        editorSelectSyntaxHighlight();
    }

    int len;
    char *buf = editorRowsToString(&len);

    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if(fd != -1) {
        if(ftruncate(fd, len) != -1) {
            if(write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                E.dirty = 0;
                editorSetStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }
    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** FIND ***/

void editorFindCallback(char *query, int key) {
    static int last_match = -1;
    static int direction = 1;

    static int saved_hl_line;
    static char *saved_hl = NULL;

    if(saved_hl) {
        memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
        free(saved_hl);
        saved_hl = NULL;
    }

    if(key == '\r' || key == '\x1b') {
        last_match = -1;
        direction = 1;
        return;
    } else if(key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    } else if(key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    } else {
        last_match = -1;
        direction = 1;
    }

    if(last_match == -1) direction = 1;
    int current = last_match;
    int i;
    for(i = 0; i < E.numrows; i++) {
        current += direction;
        if(current == -1) current = E.numrows - 1;
        else if(current == E.numrows) current = 0;

        erow *row = &E.row[current];
        char *match = strstr(row->render, query);
        if(match) {
            last_match = current;
            E.cy = current;
            E.cx = editorRowRxToCx(row, match - row->render);
            E.rowoff = E.numrows;

            saved_hl_line = current;
            saved_hl = malloc(row->rsize);
            memcpy(saved_hl, row->hl, row->rsize);            
            memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
            break;
        }
    }
}

void editorFind() {
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_coloff = E.coloff;
    int saved_rowoff = E.rowoff;


    char *query = editorPrompt("Search: %s", editorFindCallback);

    if(query) {
        free(query);
    } else {
        E.cx = saved_cx;
        E.cy = saved_cy;
        E.coloff = saved_coloff;
        E.rowoff = saved_rowoff;
    }
}

/*** APPEND BUFFER ***/

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);

    if(new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab) {
    free(ab->b);
}

/*** OUTPUT ***/

void editorScroll() {
    E.rx = 0;
    if(E.cy < E.numrows) {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }

    if(E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if(E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }

    if(E.rx < E.coloff) {
        E.coloff = E.cx;
    }
    if(E.rx >= E.coloff + E.screencols) {
        E.coloff = E.cx - E.screencols + 1;
    }
}

void editorDrawRows(struct abuf *ab) {
    if(E.mode != INSERT)
        abAppend(ab, "\x1b[1 q", 5);    
    else 
        abAppend(ab, "\x1b[5 q", 5);

    int y;
    for(y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;
        if(filerow >= E.numrows) {
            if(E.numrows == 0 && y == E.screenrows / 3) {
                char welcome [80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                        "THOR - The Text EdiTHOR");
                if(welcomelen > E.screencols) welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
                if(padding) {
                    abAppend(ab, "\x1b[94m", 5);
                    abAppend(ab, "~", 1);
                    abAppend(ab, "\x1b[m", 3);
                    padding--;
                }
                while(padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            } else if(E.numrows == 0 && y == (E.screenrows / 3) + 2) {
                char welcome [80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                        "version %s", THOR_VERSION);
                if(welcomelen > E.screencols) welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
                if(padding) {
                    abAppend(ab, "\x1b[94m", 5);
                    abAppend(ab, "~", 1);
                    abAppend(ab, "\x1b[m", 3);
                    padding--;
                }
                while(padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            } else if(E.numrows == 0 && y == (E.screenrows / 3) + 3) {
                char welcome [80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                        "made by OrangeXarot");
                if(welcomelen > E.screencols) welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
                if(padding) {
                    abAppend(ab, "\x1b[94m", 5);
                    abAppend(ab, "~", 1);
                    abAppend(ab, "\x1b[m", 3);
                    padding--;
                }
                while(padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            } else if(E.numrows == 0 && y == (E.screenrows / 3) + 5) {
                char welcome [80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                        ":help     prints help commands");
                if(welcomelen > E.screencols) welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
                if(padding) {
                    abAppend(ab, "\x1b[94m", 5);
                    abAppend(ab, "~", 1);
                    abAppend(ab, "\x1b[m", 3);
                    padding--;
                }
                while(padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            } else if(E.numrows == 0 && y == (E.screenrows / 3) + 6) {
                char welcome [80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                        ":q                  exits thor");
                if(welcomelen > E.screencols) welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
                if(padding) {
                    abAppend(ab, "\x1b[94m", 5);
                    abAppend(ab, "~", 1);
                    abAppend(ab, "\x1b[m", 3);
                    padding--;
                }
                while(padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            } else if(E.numrows == 0 && y == (E.screenrows / 3) + 7) {
                char welcome [80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                        ":w              saves the file");
                if(welcomelen > E.screencols) welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
                if(padding) {
                    abAppend(ab, "\x1b[94m", 5);
                    abAppend(ab, "~", 1);
                    abAppend(ab, "\x1b[m", 3);
                    padding--;
                }
                while(padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            
            } else if(E.numrows == 0 && y == (E.screenrows / 3) + 8) {
                char welcome [80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                        ":creds  prints all the credits");
                if(welcomelen > E.screencols) welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
                if(padding) {
                    abAppend(ab, "\x1b[94m", 5);
                    abAppend(ab, "~", 1);
                    abAppend(ab, "\x1b[m", 3);
                    padding--;
                }
                while(padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);



            
            } else {
                abAppend(ab, "\x1b[94m", 5);
                abAppend(ab, "~", 1);
                abAppend(ab, "\x1b[m", 3);
            }
        } else {
            int len = E.row[filerow].rsize - E.coloff;
            if(len < 0) len = 0;
            if(len > E.screencols) len = E.screencols;
            char *c = &E.row[filerow].render[E.coloff];
            unsigned char *hl = &E.row[filerow].hl[E.coloff];
            int current_color = -1;
            int j;
            for(j = 0; j < len; j++) {
                if(iscntrl(c[j])) {
                    char sym = (c[j] <= 26) ? '@' : '?';
                    abAppend(ab, "\x1b[7m", 4);
                    abAppend(ab, &sym, 1);
                    abAppend(ab, "\x1b[m", 3);
                    if(current_color != -1) {
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
                        abAppend(ab, buf, clen);
                    }
                } else if(hl[j] == HL_NORMAL) {
                    if(current_color != -1) {
                        abAppend(ab, "\x1b[39m", 5);
                        abAppend(ab, "\x1b[49m", 5);
                        current_color = -1;
                    }
                    abAppend(ab, &c[j], 1);
                } else {
                    int color = editorSyntaxToColor(hl[j]);
                    if(color != current_color) {
                        current_color = color;
                        char buf[16];
                        int clen;
                        if (color == 43) clen = snprintf(buf, sizeof(buf), "\x1b[30;%dm", color);
                        else clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        abAppend(ab, buf, clen);
                    }
                    abAppend(ab, &c[j], 1);

                }
            }
            abAppend(ab, "\x1b[39m", 5);
            abAppend(ab, "\x1b[49m", 5);
        }
        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab) {
    abAppend(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), 
            " %.20s%s - %d lines", 
            E.filename ? E.filename : "[New File]", E.dirty ? "*" : "", E.numrows); 

    int perc;
    if(E.numrows <= E.screenrows) perc = 100;
    else perc = round(100 * E.rowoff / (E.numrows - E.screenrows));

    if(perc > 100) perc = 100;
    else if(perc < 0) perc = 100;

    int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d%% %d,%d ", 
            E.syntax ? E.syntax->filetype : "filetype not detected", perc, E.cy + 1, E.cx + 1);
    if(len > E.screencols) len = E.screencols;
    abAppend(ab, status, len);

    while(len < E.screencols) {
        if(E.screencols - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if(msglen > E.screencols) msglen = E.screencols;
    if(msglen && time(NULL) - E.statusmsg_time < 5) {
        int padding = (E.screencols - msglen) / 2;
        while(padding--) abAppend(ab, " ", 1);
        abAppend(ab, E.statusmsg, msglen);
    }
}

void editorRefreshScreen() {
    if(getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
    E.screenrows -= 2;

    editorScroll();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, 
            (E.rx - E.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

/*** INPUT ***/

char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
    size_t bufsize = 128;
    char *buf = malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0';

    while(1) {
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();

        int c = editorReadKey();
        if(c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
            if(buflen != 0) buf[--buflen] = '\0';
        } else if(c == '\x1b') {
            editorSetStatusMessage("");
            if(callback) callback(buf, c);
            free(buf);
            return NULL;
            break;
        } else if(c == '\r') {
            if(buflen != 0) {
                editorSetStatusMessage("");
                if(callback) callback(buf, c);
                return buf;
                break;
            }
        } else if(!iscntrl(c) && c < 128) {
            if(buflen == bufsize - 1) {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }

        if(callback) callback(buf, c);
    }
    free(buf);
    return NULL;
}

void editorMoveCursor(int key) {
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    switch(key) {
        case ARROW_LEFT:
            if(E.cx != 0) {
                E.cx--;
            } else if(E.cy > 0) {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            if(row && E.cx < row->size) {
                E.cx++;
            } else if(row && E.cx == row->size) {
                E.cy++;
                E.cx = 0;
            }
            break;
        case ARROW_UP:
            if(E.cy != 0) {
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if(E.cy < E.numrows) {
                E.cy++;
            }
            break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if(E.cx > rowlen) {
        E.cx = rowlen;
    }
}

void editorScrollKey(int key) {
    switch(key) {
        case SCROLL_DOWN:
            if(E.rowoff == E.numrows) return;
             
            if(E.cy == E.rowoff) E.cy++;
            E.rowoff++;

            break;
        case SCROLL_UP:
            if(E.rowoff == 0) return;
                
            if(E.cy == E.rowoff + E.screenrows - 2) E.cy--;
            E.rowoff--;
            
            break;
    }

}

void editorCommand() {
    char *command = editorPrompt("Command: :%s", NULL);

    if(command){

        if(isdigit(command[0])) {
            int is_number = 1;
            int comlen = strlen(command);
            
            for(int i = 0; i < comlen; i++) {
                if(!isdigit(command[i])) {
                    is_number = 0;
                }
            }
            if(is_number == 1) {
                int line = atoi(command);
                if(line < E.numrows) { 
                    if(line > E.cy) {
                        while(E.cy != line - 1) {
                            editorMoveCursor(ARROW_DOWN);
                        }
                    } else if(line < E.cy) {
                        while(E.cy != line - 1) {
                            editorMoveCursor(ARROW_UP);
                        }
                    }
                }
            }

        } else if(command[0] == 'w' && command[1] == 'q') { 
            editorSave();

            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);

            exit(0); 
        } else if(command[0] == 'w') {
            editorSave();
        } else if(command[0] == 'q') {
            if(E.dirty && command[1] != '!') {
                editorSetStatusMessage("Unsaved Changes Detected (use ! to override)");
            } else {
                write(STDOUT_FILENO, "\x1b[2J", 4);
                write(STDOUT_FILENO, "\x1b[H", 3);

                exit(0);
            } 
        } else if(strcmp(command, "help") == 0) editorSetStatusMessage(":help quit | :help editor | :help other");
        else if(strcmp(command, "help quit") == 0) editorSetStatusMessage(":q = quit | :q! = override quit | :w = save | :wq = save and quit");
        else if(strcmp(command, "help editor") == 0) editorSetStatusMessage(":num = goto line num | / = search");
        else if(strcmp(command, "help other") == 0) editorSetStatusMessage(":help = shows help | :creds = shows credits");
        else if(strcmp(command, "creds") == 0) editorSetStatusMessage("Made by OrangeXarot, Named by i._.tram");
        else {
            editorSetStatusMessage("Invalid Syntax \":%s\"", command);
        }
    }
}

void editorYankPrompt(int at) {
    char *clines = editorPrompt("Yanking: %s", NULL);
    int lines;
    
    if(clines == NULL) return;

    if(clines[0] == 'y') {
        lines = 1;
        editorYankRow(at, lines);
    } else {
        int is_number = 1;
        int comlen = strlen(clines);

        for(int i = 0; i < comlen; i++) {
            if(!isdigit(clines[i])) {
                is_number = 0;
            }
        }
        if(is_number == 1) {
            lines = atoi(clines);
            editorYankRow(at, lines);
        }
    }
}

void editorDelPrompt(int at) {    
    char *clines = editorPrompt("Deleting: %s", NULL);
    int lines;
    
    if(clines == NULL) return;

    if(clines[0] == 'd') {
        lines = 1;
        editorDelYankRow(at, lines);
    } else {
        int is_number = 1;
        int comlen = strlen(clines);

        for(int i = 0; i < comlen; i++) {
            if(!isdigit(clines[i])) {
                is_number = 0;
            }
        }
        if(is_number == 1) {
            lines = atoi(clines);
            editorDelYankRow(at, lines);
        }
    }
    free(clines);
}

void editorChangeMode(int mode) {
    if(mode == INSERT)
        editorSetStatusMessage("-- INSERT MODE --");
    else 
        editorSetStatusMessage("");

    E.mode = mode; 
}

void editorProcessKeypress() {    
    int c = editorReadKey();

    if(E.mode == INSERT) {
        editorSetStatusMessage("-- INSERT MODE --");
        switch(c) {
           case '\r':
                editorInsertNewline();
                break;

            case HOME_KEY:
                E.cx = 0;
                break;

            case END_KEY:
                if(E.cy < E.numrows)
                    E.cx = E.row[E.cy].size;
                break;

            case BACKSPACE:
            case DEL_KEY:
                if(c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
                editorDelChar();
                break;

            case PAGE_UP:
            case PAGE_DOWN: 
                {
                    if(c == PAGE_UP) {
                        E.cy = E.rowoff;
                    } else if(c == PAGE_DOWN) {
                        E.cy = E.rowoff + E.screenrows - 1;
                        if(E.cy > E.numrows) E.cy = E.numrows;
                    }

                    int times = E.screenrows;
                    while(times--) 
                        editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
                break;
            case ARROW_LEFT:
            case ARROW_RIGHT:
            case ARROW_UP:
            case ARROW_DOWN:
                editorMoveCursor(c);
                break;

            case '\t':
                editorInsertChar(' ');
                editorInsertChar(' ');
                editorInsertChar(' ');
                editorInsertChar(' ');
                break; 
   
            case SCROLL_UP:
            case SCROLL_DOWN:
                editorScrollKey(c);
                break;


            case CTRL_KEY('l'):
            case '\x1b':
                editorChangeMode(COMMAND);
                editorSetStatusMessage("");
                break;

            case '(':
                editorInsertChar(c);
                editorInsertChar(')');
                editorMoveCursor(ARROW_LEFT);
                break;

            case '[':
                editorInsertChar(c);
                editorInsertChar(']');
                editorMoveCursor(ARROW_LEFT);
                break;

            case '{':
                editorInsertChar(c);
                editorInsertChar('}');
                editorMoveCursor(ARROW_LEFT);
                break;

            case '"':
                editorInsertChar(c);
                editorInsertChar('"');
                editorMoveCursor(ARROW_LEFT);
                break;

            case '\'':
                editorInsertChar(c);
                editorInsertChar('\'');
                editorMoveCursor(ARROW_LEFT);
                break;



            default:
                editorInsertChar(c);
                break;
        }
    } else if(E.mode == COMMAND) {
        switch(c) {

            case DEL_KEY:
            case 'x':
                editorMoveCursor(ARROW_RIGHT);
                editorDelChar();
                editorSetStatusMessage("To lazy to enter insert mode huh?");
                break;

            case 'X':
                editorDelChar();
                break;            

            case PAGE_UP:
            case PAGE_DOWN: 
                {
                    if(c == PAGE_UP) {
                        E.cy = E.rowoff;
                    } else if(c == PAGE_DOWN) {
                        E.cy = E.rowoff + E.screenrows - 1;
                        if(E.cy > E.numrows) E.cy = E.numrows;
                    }

                    int times = E.screenrows;
                    while(times--) 
                        editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
                break;

            case 'g':
                E.cy = 0;
                editorSetStatusMessage("The Beginning Of Time");
                break;          

            case 'G':
                E.cy = E.numrows;
                editorSetStatusMessage("The End Of Time");
                break;

            case ARROW_LEFT:
            case ARROW_RIGHT:
            case ARROW_UP:
            case ARROW_DOWN:
                editorMoveCursor(c);
                break;

            case ',':
                    for(int i = 0; i < 5;i++)
                    editorMoveCursor(ARROW_LEFT);
                break;

            case '.':
                    for(int i = 0; i < 5;i++)
                    editorMoveCursor(ARROW_RIGHT);
                break;

            case S_ARROW_LEFT:
            case S_ARROW_RIGHT:
            case S_ARROW_UP:
            case S_ARROW_DOWN:
                editorMoveCursor(c);
                editorMoveCursor(c);
                editorMoveCursor(c);
                editorMoveCursor(c);
                break;

            case SCROLL_UP:
            case SCROLL_DOWN:
                editorScrollKey(c);
                break;

            case HOME_KEY:
                E.cx = 0;
                break;

            case END_KEY:
                if(E.cy < E.numrows)
                    E.cx = E.row[E.cy].size;
                break;


            case 'i':
                editorChangeMode(INSERT);
                break;

            case ':':
                editorCommand();
                break;
            case '/':
                editorFind();
                break;

            case 'o':
                editorInsertRow(E.cy + 1, "", 0);
                editorMoveCursor(ARROW_DOWN);
                editorChangeMode(INSERT);
                break;

            case 'O':
                editorInsertRow(E.cy, "", 0);
                if(E.cx > 0) editorMoveCursor(ARROW_LEFT);
                editorChangeMode(INSERT);
                break;

            case 'd':
                editorDelPrompt(E.cy);
                break;

            case 'y':
                editorYankPrompt(E.cy);
                break;

            case 'p':
                editorPasteRows();
                break;
        }
    }
}


/*** INIT ***/

void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.cprow = NULL;
    E.dirty = 0;
    E.mode = COMMAND;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    E.syntax = NULL;

    if(getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
    E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if(argc >= 2) {
        editorOpen(argv[1]);
    }

    while(1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
