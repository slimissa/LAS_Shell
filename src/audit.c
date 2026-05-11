/*
 * audit.c -- Las_shell Phase 4.1: Audit Mode & Compliance Log
 *
 * ==========================================================================
 * DESIGN OVERVIEW
 * ==========================================================================
 *
 * When launched with `las_shell --audit [script.sh]`, every command executed
 * is logged to ~/.las_shell_audit.  Each record contains:
 *
 *   timestamp     ??? ISO 8601 UTC  (2024-01-15T09:30:00Z)
 *   username      ??? from getpwuid(getuid())
 *   pid           ??? shell process ID (ties parallel sessions apart)
 *   exit_code     ??? integer exit status of the command
 *   stdout_hash   ??? SHA-256 hex of captured stdout  (64 hex chars)
 *   chain_hash    ??? SHA-256 of (prev_chain_hash || record_body)
 *                   Provides a tamper-evident hash chain: altering any past
 *                   record breaks every chain_hash that follows it.
 *   command       ??? full command string, CSV-double-quoted
 *
 * CSV format (one line per record):
 *   timestamp,username,pid,exit_code,stdout_hash,chain_hash,"command"
 *
 * Example:
 *   2024-01-15T09:30:00Z,slim,18342,0,a3f2c1d...,7b9e4a2...,"order buy SPY 100 market"
 *
 * -- Regulatory compliance ------------------------------------------------
 *   SEC Rule 17a-4(f)  -- immutable, time-stamped electronic records
 *   MiFID II Art. 25   -- records of orders and transactions
 *
 * -- File safety ---------------------------------------------------------
 *   The log file is opened O_WRONLY | O_CREAT | O_APPEND only.
 *   It is NEVER truncated.  Each write(2) is atomic on Linux for writes
 *   <= PIPE_BUF (4096 bytes) -- records are kept within that limit.
 *
 * -- SHA-256 implementation ----------------------------------------------
 *   Primary:  OpenSSL EVP_Digest  (-DUSE_OPENSSL_SHA -lssl -lcrypto)
 *   Fallback: Portable FIPS 180-4 implementation compiled in.
 *
 * -- Chain verification --------------------------------------------------
 *   audit_verify_log() replays the chain: for each record it recomputes
 *   chain_hash_n = SHA256(chain_hash_{n-1} || body_without_chain_field)
 *   and compares to the stored value.  Any insertion, deletion, or field
 *   edit breaks the chain from that point forward.
 *
 * ==========================================================================
 */

#define _POSIX_C_SOURCE 200809L

#include "../include/my_own_shell.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <pwd.h>
#include <errno.h>
#include <stdint.h>
#include <ctype.h>

#ifdef USE_OPENSSL_SHA
#  include <openssl/evp.h>
#endif

/* ==========================================================================
 * ??1  PORTABLE SHA-256  (FIPS 180-4 / RFC 6234)
 * ========================================================================== */

#ifndef USE_OPENSSL_SHA

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buf[64];
    uint32_t buf_len;
} SHA256CTX;

static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,
    0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,
    0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,
    0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,
    0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,
    0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROR32(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define CH(x,y,z)  (((x)&(y))^(~(x)&(z)))
#define MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define EP0(x)     (ROR32(x,2)^ROR32(x,13)^ROR32(x,22))
#define EP1(x)     (ROR32(x,6)^ROR32(x,11)^ROR32(x,25))
#define SIG0(x)    (ROR32(x,7)^ROR32(x,18)^((x)>>3))
#define SIG1(x)    (ROR32(x,17)^ROR32(x,19)^((x)>>10))

static void sha256_compress(uint32_t st[8], const uint8_t blk[64]) {
    uint32_t W[64],a,b,c,d,e,f,g,h,t1,t2;
    for (int i=0;i<16;i++)
        W[i]=((uint32_t)blk[i*4]<<24)|((uint32_t)blk[i*4+1]<<16)
            |((uint32_t)blk[i*4+2]<<8)|(uint32_t)blk[i*4+3];
    for (int i=16;i<64;i++) W[i]=SIG1(W[i-2])+W[i-7]+SIG0(W[i-15])+W[i-16];
    a=st[0];b=st[1];c=st[2];d=st[3];e=st[4];f=st[5];g=st[6];h=st[7];
    for (int i=0;i<64;i++){
        t1=h+EP1(e)+CH(e,f,g)+K256[i]+W[i]; t2=EP0(a)+MAJ(a,b,c);
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    st[0]+=a;st[1]+=b;st[2]+=c;st[3]+=d;
    st[4]+=e;st[5]+=f;st[6]+=g;st[7]+=h;
}

static void sha256_init(SHA256CTX *c) {
    c->state[0]=0x6a09e667;c->state[1]=0xbb67ae85;
    c->state[2]=0x3c6ef372;c->state[3]=0xa54ff53a;
    c->state[4]=0x510e527f;c->state[5]=0x9b05688c;
    c->state[6]=0x1f83d9ab;c->state[7]=0x5be0cd19;
    c->count=0;c->buf_len=0;
}

static void sha256_update(SHA256CTX *c, const uint8_t *d, size_t len) {
    for (size_t i=0;i<len;i++){
        c->buf[c->buf_len++]=d[i]; c->count++;
        if (c->buf_len==64){sha256_compress(c->state,c->buf);c->buf_len=0;}
    }
}

static void sha256_final(SHA256CTX *c, uint8_t digest[32]) {
    uint64_t bits=c->count*8; uint8_t pad=0x80;
    sha256_update(c,&pad,1);
    while(c->buf_len!=56){pad=0;sha256_update(c,&pad,1);}
    uint8_t bc[8];
    for(int i=7;i>=0;i--){bc[i]=(uint8_t)(bits&0xff);bits>>=8;}
    sha256_update(c,bc,8);
    for(int i=0;i<8;i++){
        digest[i*4]=(uint8_t)(c->state[i]>>24);
        digest[i*4+1]=(uint8_t)(c->state[i]>>16);
        digest[i*4+2]=(uint8_t)(c->state[i]>>8);
        digest[i*4+3]=(uint8_t)(c->state[i]);
    }
}

static void compute_sha256(const uint8_t *data, size_t len, uint8_t digest[32]) {
    SHA256CTX ctx; sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

#else /* USE_OPENSSL_SHA */

static void compute_sha256(const uint8_t *data, size_t len, uint8_t digest[32]) {
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) { memset(digest,0,32); return; }
    unsigned int dlen = 32;
    EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(mdctx, data, len);
    EVP_DigestFinal_ex(mdctx, digest, &dlen);
    EVP_MD_CTX_free(mdctx);
}

#endif /* USE_OPENSSL_SHA */

/* ==========================================================================
 * ??2  MODULE STATE
 * ========================================================================== */

static int  g_audit_enabled = 0;
static char g_audit_path[512];

/* Genesis chain seed: 64 hex zeros */
static char g_chain_hash[65] =
    "0000000000000000000000000000000000000000000000000000000000000000";

/* ==========================================================================
 * ??3  INTERNAL HELPERS
 * ========================================================================== */

static void bytes_to_hex(const uint8_t *b, int n, char *out) {
    static const char hx[]="0123456789abcdef";
    for (int i=0;i<n;i++){out[i*2]=hx[(b[i]>>4)&0xf];out[i*2+1]=hx[b[i]&0xf];}
    out[n*2]='\0';
}

static int is_valid_hex64(const char *s) {
    if (!s || strlen(s)!=64) return 0;
    for (int i=0;i<64;i++) if (!isxdigit((unsigned char)s[i])) return 0;
    return 1;
}

static void now_iso8601(char *buf, size_t sz) {
    time_t t=time(NULL); struct tm *u=gmtime(&t);
    strftime(buf, sz, "%Y-%m-%dT%H:%M:%SZ", u);
}

static const char *current_user(void) {
    struct passwd *pw = getpwuid(getuid());
    return pw ? pw->pw_name : "unknown";
}

/*
 * csv_quote() -- RFC 4180 CSV quoting
 *
 * Wraps cmd in double-quotes. Inner " are doubled ("") per RFC 4180.
 * This makes records parseable by Python csv, Excel, pandas, etc.
 * without any custom escaping logic.
 */
static void csv_quote(const char *cmd, char *out, size_t sz) {
    size_t i = 0;
    out[i++] = '"';
    for (size_t j = 0; cmd[j] && i < sz - 3; j++) {
        if (cmd[j] == '"') {
            if (i < sz - 4) { out[i++] = '"'; out[i++] = '"'; }
        } else {
            out[i++] = cmd[j];
        }
    }
    out[i++] = '"';
    out[i]   = '\0';
}

/* -- Chain hash update ---------------------------------------------------
 * new_chain = SHA256( prev_chain_hex_64 || record_body_utf8 )
 * chain_hash is advanced in g_chain_hash.
 */
static void chain_advance(const char *body) {
    size_t pl = 64, bl = strlen(body);
    uint8_t *in = malloc(pl+bl);
    if (!in) {
        fprintf(stderr, "[audit] ERROR: chain_advance malloc failed — audit chain not advanced\n");
        return;
    }
    memcpy(in, g_chain_hash, pl);
    memcpy(in+pl, body, bl);
    uint8_t digest[32];
    compute_sha256(in, pl+bl, digest);
    free(in);
    bytes_to_hex(digest, 32, g_chain_hash);
}

/* Atomic append to the log (single write call) */
static void log_write(const char *rec) {
    int fd = open(g_audit_path, O_WRONLY|O_CREAT|O_APPEND, 0600);
    if (fd<0) {
        fprintf(stderr,"[audit] ERROR: write failed '%s': %s\n",
                g_audit_path, strerror(errno));
        return;
    }
        size_t len=strlen(rec);
    if (len > 4096)
        fprintf(stderr, "[audit] WARNING: record exceeds PIPE_BUF (%zu > 4096) — write may not be atomic\n", len);
    if (write(fd,rec,len) != (ssize_t)len)
        fprintf(stderr,"[audit] WARNING: partial write: %s\n",strerror(errno));
    close(fd);
}

/* ==========================================================================
 * ??4  audit_init()
 *
 * Call once on shell startup when --audit flag is detected.
 * Seeds chain from last record of any existing log (session continuity).
 * Writes a SESSION_START comment marker.
 * ========================================================================== */

void audit_init(const char *log_path) {
    /* Resolve path */
    if (log_path && log_path[0]) {
        strncpy(g_audit_path, log_path, sizeof(g_audit_path)-1);
        g_audit_path[sizeof(g_audit_path)-1]='\0';
    } else {
        const char *home = getenv("HOME");
        if (!home) home="/tmp";
        snprintf(g_audit_path, sizeof(g_audit_path), "%s/.las_shell_audit", home);
    }

    /* Verify/create file */
    int fd = open(g_audit_path, O_WRONLY|O_CREAT|O_APPEND, 0600);
    if (fd<0) {
        fprintf(stderr,"[audit] FATAL: cannot open log '%s': %s\n",
                g_audit_path, strerror(errno));
        return;
    }
    close(fd);

    /* Seed chain from last data record (cross-session continuity) */
    {
        FILE *f = fopen(g_audit_path, "r");
        if (f) {
            char last[65]={0}, line[4096];
            while (fgets(line, sizeof(line), f)) {
                if (line[0]=='#'||line[0]=='\n') continue;
                /* Field 5 (0-indexed) is chain_hash.
                 * Walk 5 commas; grab the next 64-char token. */
                int commas=0, in_q=0;
                const char *p=line;
                while (*p && commas<5) {
                    if (*p=='"') in_q=!in_q;
                    if (!in_q && *p==',') commas++;
                    p++;
                }
                char cand[65]={0}; int ci=0;
                while (*p && *p!=',' && *p!='\n' && ci<64)
                    cand[ci++]=*p++;
                cand[ci]='\0';
                if (is_valid_hex64(cand))
                    strncpy(last, cand, 64);
            }
            fclose(f);
            if (last[0]) strncpy(g_chain_hash, last, 65);
        }
    }

    g_audit_enabled = 1;

    /* SESSION_START marker */
    char ts[32]; now_iso8601(ts, sizeof(ts));
    char marker[512];
    snprintf(marker, sizeof(marker),
        "# SESSION_START ts=%s user=%s pid=%d chain_seed=%.16s...\n",
        ts, current_user(), (int)getpid(), g_chain_hash);
    log_write(marker);

    fprintf(stderr, "[audit] enabled  ???  %s\n", g_audit_path);
}

int audit_is_enabled(void) { return g_audit_enabled; }

/* ==========================================================================
 * ??5  audit_log_command()
 *
 * Wraps execute_command_line(), capturing stdout for hashing.
 * The user still sees all output -- it is echoed after capture.
 *
 * Record format written to log:
 *   ts,user,pid,exit,stdout_sha256,chain_sha256,"cmd"\n
 *
 * Chain input:
 *   SHA256( g_chain_hash_prev_64_hex || "ts,user,pid,exit,stdout_sha256,\"cmd\"\n" )
 *
 * The chain input intentionally excludes chain_hash itself so the field
 * cannot be forged without knowing the prior chain state.
 * ========================================================================== */

#define AUDIT_CAP (64 * 1024)  /* 64 KB stdout capture cap */

int audit_log_command(const char *cmd, char **env) {
    /* Skip logging for null, empty, or whitespace-only commands */
    if (!g_audit_enabled || !cmd || !cmd[0])
        return execute_command_line((char*)cmd, env);
    {
        const char *p = cmd;
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (!*p) return execute_command_line((char*)cmd, env);
    }

    /* -- 1. Set up stdout capture pipe -- */
    int pfd[2];
    if (pipe(pfd)!=0) {
        perror("[audit] pipe");
        return execute_command_line((char*)cmd, env);
    }

    int saved_out = dup(STDOUT_FILENO);
    if (saved_out<0) {
        close(pfd[0]); close(pfd[1]);
        return execute_command_line((char*)cmd, env);
    }
    if (dup2(pfd[1], STDOUT_FILENO)<0) {
        close(pfd[0]); close(pfd[1]); close(saved_out);
        return execute_command_line((char*)cmd, env);
    }
    close(pfd[1]);

    /* -- 2. Execute (full shell machinery: pipes, operators, builtins) -- */
    int exit_code = execute_command_line((char*)cmd, env);

    /* -- 3. Restore stdout before draining pipe -- */
    fflush(stdout);
    dup2(saved_out, STDOUT_FILENO);
    close(saved_out);

    /* -- 4. Drain capture pipe -- */
    char *cap = malloc(AUDIT_CAP + 1);
    size_t cap_len = 0;
    if (cap) {
        ssize_t n;
        while (cap_len < AUDIT_CAP &&
               (n=read(pfd[0], cap+cap_len, AUDIT_CAP-cap_len)) > 0)
            cap_len += (size_t)n;
        cap[cap_len] = '\0';
    }
    close(pfd[0]);

    /* -- 5. Echo captured output to user -- */
    if (cap && cap_len > 0)
        write(STDOUT_FILENO, cap, cap_len);

    /* -- 6. SHA-256 of stdout -- */
    uint8_t stdout_digest[32];
    compute_sha256(
        cap ? (const uint8_t*)cap : (const uint8_t*)"",
        cap_len, stdout_digest);
    char stdout_hex[65];
    bytes_to_hex(stdout_digest, 32, stdout_hex);
    if (cap) free(cap);

    /* -- 7. Build record body (excludes chain_hash field) -- */
    char ts[32]; now_iso8601(ts, sizeof(ts));
    char cmd_q[2048]; csv_quote(cmd, cmd_q, sizeof(cmd_q));

    /*
     * body = "ts,user,pid,exit,stdout_hex,\"cmd\"\n"
     * This is SHA256-chained: chain_n = SHA256(chain_{n-1} || body)
     */
    char body[4096];
    snprintf(body, sizeof(body), "%s,%s,%d,%d,%s,%s\n",
             ts, current_user(), (int)getpid(),
             exit_code, stdout_hex, cmd_q);

    /* -- 8. Advance chain hash -- */
    chain_advance(body);

    /* -- 9. Inject chain_hash as field 5 in the final record --
     *
     * body = "f0,f1,f2,f3,f4,f5\n"  (6 fields)
     *                     ^f4=stdout_hex  ^f5="cmd"
     * We need: "f0,f1,f2,f3,f4,chain_hash,f5\n"
     *
     * Strategy: find the 5th comma (between f4 and f5) and insert.
     */
    {
        int commas=0, in_q=0, split=-1;
        for (int i=0; body[i] && body[i]!='\n'; i++) {
            if (body[i]=='"') in_q=!in_q;
            if (!in_q && body[i]==',') {
                commas++;
                if (commas==5) { split=i; break; }
            }
        }

        char final_rec[4200];
        if (split>0) {
            /* prefix = body[0..split-1], suffix = body[split..] */
            char prefix[2048]; strncpy(prefix, body, (size_t)split);
            prefix[split]='\0';
            const char *suffix = body+split;    /* includes leading comma */
            snprintf(final_rec, sizeof(final_rec),
                     "%s,%s%s", prefix, g_chain_hash, suffix);
        } else {
            /* Fallback: chain_hash appended (should not happen) */
            size_t bl=strlen(body);
            if (bl>0 && body[bl-1]=='\n') body[bl-1]='\0';
            snprintf(final_rec, sizeof(final_rec),
                     "%s,%s\n", body, g_chain_hash);
        }

        log_write(final_rec);
    }

    return exit_code;
}

/* ==========================================================================
 * ??6  audit_verify_log()
 *
 * Replays chain hash from genesis and flags any break.
 *
 * Returns 0 = all ok, 1 = integrity violation detected.
 * ========================================================================== */

int audit_verify_log(const char *log_path) {
    const char *path = (log_path && log_path[0]) ? log_path : NULL;
    char fallback[512];
    if (!path) {
        const char *home = getenv("HOME"); if (!home) home="/tmp";
        snprintf(fallback, sizeof(fallback), "%s/.las_shell_audit", home);
        path = fallback;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "audit verify: cannot open '%s': %s\n",
                path, strerror(errno));
        return 1;
    }

    printf("Verifying: %s\n", path);
    printf("%.78s\n",
           "--------------------------------------------------------------------------------");

    char chain[65] =
        "0000000000000000000000000000000000000000000000000000000000000000";

    int total=0, ok=0, bad=0, mal=0;
    int first_broken = -1;
    char line[4096];

    while (fgets(line, sizeof(line), f)) {
        if (line[0]=='#' || line[0]=='\n') continue;
        total++;

        /*
         * Parse the 7 CSV fields.
         * ts,user,pid,exit,stdout_hash,chain_hash,"cmd"
         *  0   1   2   3       4            5       6
         */
        char fs[8][1024]; int nf=0;
        {
            const char *p=line;
            while (*p && nf<8) {
                char *d=fs[nf]; size_t di=0;
                if (*p=='"') {
                    p++;
                    while (*p && di<1023) {
                        if (*p=='"' && *(p+1)=='"') { d[di++]='"'; p+=2; continue; } /* RFC 4180 "" */
                        if (*p=='"') break;
                        d[di++]=*p++;
                    }
                    if (*p=='"') p++;
                } else {
                    while (*p && *p!=',' && *p!='\n' && di<1023)
                        d[di++]=*p++;
                }
                d[di]='\0'; nf++;
                if (*p==',') p++;
            }
        }

        if (nf<6) {
            printf("[MALFORMED  ] #%-4d (fields=%d)\n", total, nf);
            mal++; continue;
        }

        const char *ts      = fs[0];
        const char *user    = fs[1];
        /* pid   = fs[2] */
        const char *exit_s  = fs[3];
        const char *sh      = fs[4]; /* stdout_hash */
        const char *ch      = fs[5]; /* chain_hash stored */
        const char *cmd_s   = (nf>=7) ? fs[6] : "";

        if (!is_valid_hex64(sh) || !is_valid_hex64(ch)) {
            printf("[MALFORMED  ] #%-4d %s -- invalid hash field\n", total, ts);
            mal++; continue;
        }

        /*
         * Reconstruct the body string that was chain-hashed.
         * It's the original line with the chain_hash field (field 5) removed.
         *
         * Original: f0,f1,f2,f3,f4,chain,f5\n
         * Body:     f0,f1,f2,f3,f4,f5\n
         *
         * Find comma 4 (between f4 and chain) and comma 5 (between chain and f5).
         */
        char body[4096];
        {
            int commas=0, in_q=0, c4=-1, c5=-1;
            for (int i=0; line[i] && line[i]!='\n'; i++) {
                if (line[i]=='"') in_q=!in_q;
                if (!in_q && line[i]==',') {
                    commas++;
                    if (commas==5) c4=i;
                    if (commas==6) { c5=i; break; }
                }
            }
            if (c4<0 || c5<0) {
                printf("[MALFORMED  ] #%-4d %s -- cannot parse structure\n",
                       total, ts);
                mal++; continue;
            }
            /* body = line[0..c4] + line[c5..end] */
            /* i.e. strip the chain_hash field between comma4 and comma5 */
            char suffix[2048];
            strncpy(suffix, line+c5, sizeof(suffix)-1);
            suffix[sizeof(suffix)-1]='\0';
            /* Remove trailing newline from suffix for re-insertion */
            snprintf(body, sizeof(body), "%.*s%s", c4, line, suffix);
            /* Ensure body ends in \n */
            size_t bl=strlen(body);
            if (bl==0 || body[bl-1]!='\n') { body[bl]='\n'; body[bl+1]='\0'; }
        }

        /* Recompute chain_n = SHA256(chain_{n-1} || body) */
        size_t pl=64, bl=strlen(body);
        uint8_t *in = malloc(pl+bl);
        char expected[65]={0};
        if (in) {
            memcpy(in, chain, pl);
            memcpy(in+pl, body, bl);
            uint8_t dig[32];
            compute_sha256(in, pl+bl, dig);
            free(in);
            bytes_to_hex(dig, 32, expected);
        }

        int chain_ok = (strncmp(expected, ch, 64)==0);
        if (chain_ok) {
            printf("[OK          ] #%-4d  %s  %-10s  exit=%-3s  stdout=%.8s  chain=%.8s\n",
                   total, ts, user, exit_s, sh, ch);
            ok++;
            strncpy(chain, ch, 65);
        } else {
            if (first_broken<0) first_broken=total;
            printf("[CHAIN_BROKEN] #%-4d  %s  %-10s  exit=%-3s  cmd=%s\n"
                   "               expected=%.16s...\n"
                   "               stored  =%.16s...\n",
                   total, ts, user, exit_s, cmd_s,
                   expected, ch);
            bad++;
            strncpy(chain, ch, 65);
        }
    }
    fclose(f);

    printf("%.78s\n",
           "--------------------------------------------------------------------------------");
    printf("Records: %d  OK: %d  CHAIN_BROKEN: %d  MALFORMED: %d\n",
           total, ok, bad, mal);

    if (bad==0 && mal==0) {
        printf("RESULT: INTEGRITY OK -- log is tamper-free\n");
        return 0;
    }
    if (first_broken>=0)
        printf("RESULT: INTEGRITY VIOLATION -- chain broken from record #%d\n",
               first_broken);
    else
        printf("RESULT: INTEGRITY VIOLATION -- %d malformed record(s)\n", mal);
    return 1;
}

/* ==========================================================================
 * ??7  audit_show_log()
 *
 * Pretty-print the last N data records from the log.
 * Called by the `audit show [N]` built-in.
 * ========================================================================== */

void audit_show_log(int tail_n) {
    char path[512];
    if (g_audit_path[0]) {
        strncpy(path, g_audit_path, sizeof(path)-1);
        path[sizeof(path)-1]='\0';
    } else {
        const char *home=getenv("HOME"); if (!home) home="/tmp";
        snprintf(path, sizeof(path), "%s/.las_shell_audit", home);
    }

    FILE *f = fopen(path, "r");
    if (!f) { printf("No audit log at %s\n", path); return; }

    char **lines=NULL; int count=0, cap=0; char buf[4096];
    while (fgets(buf, sizeof(buf), f)) {
        if (buf[0]=='#' || buf[0]=='\n') continue;
        if (count>=cap) {
            cap = cap ? cap*2 : 64;
            lines = realloc(lines, cap*sizeof(char*));
        }
        lines[count++] = strdup(buf);
    }
    fclose(f);

    int start = (tail_n>0 && tail_n<count) ? count-tail_n : 0;
    printf("Audit log: %s  (%d records, showing %d)\n",
           path, count, count-start);
    printf("%.78s\n",
           "--------------------------------------------------------------------------------");

    for (int i=start; i<count; i++) {
        char ts[32]={0}, user[32]={0}, pid[16]={0}, exit_s[8]={0};
        sscanf(lines[i], "%31[^,],%31[^,],%15[^,],%7[^,]",
               ts, user, pid, exit_s);
        /* Extract command: find the first " in the line */
        char cmd_preview[80]={0};
        const char *q1=strchr(lines[i],'"');
        if (q1) {
            q1++;
            const char *q2=lines[i]+strlen(lines[i])-1;
            while (q2>q1 && *q2!='"') q2--;
            size_t clen=(q2>q1)?(size_t)(q2-q1):0;
            if (clen>79) clen=79;
            strncpy(cmd_preview, q1, clen);
        }
        printf("#%-4d  %s  %-12s  exit=%-3s  %s\n",
               i+1, ts, user, exit_s, cmd_preview);
        free(lines[i]);
    }
    free(lines);
    printf("%.78s\n",
           "--------------------------------------------------------------------------------");
}

/* audit_get_path() — returns the active audit log path, or default if not set */
const char *audit_get_path(void) {
    if (g_audit_path[0]) return g_audit_path;
    static char default_path[512];
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(default_path, sizeof(default_path), "%s/.las_shell_audit", home);
    return default_path;
}
