#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <syslog.h>
#include <paths.h>
#include <fcntl.h>
#include <pcre.h>
#include <time.h>
#include <mysql/mysql.h>

#define CONFIG_FILE "/etc/dhcplogd.conf"
#define PID_FILE "/var/run/dhcplogd.pid"

/* Fix wrong MAC statements, such as 0:4:61:6c:80:7 */
static void normalize_log(char *str, int len) {
    char mon[4];
    char time[16];
    char host[128];
    char proc[64];
    char leased_ip[16];
    char hostname[128];
    char relay[16];
    char sw_ip[16];
    int day;
    int hostmac[6];
    int sw_mac[6];
    int eth;
    int port;
    int vlan;

    if (sscanf(str, "%4s %d %16s %128s %64s BIND for HOSTMAC %x:%x:%x:%x:%x:%x LEASED-IP %16s "
	    "HOSTNAME %128s SW-MAC %x:%x:%x:%x:%x:%x RELAY %16s Circuit-ID (%s eth %d/%d:%d)",
	    mon, &day, time, host, proc, &hostmac[0], &hostmac[1], &hostmac[2], &hostmac[3], &hostmac[4], &hostmac[5], leased_ip,
	    hostname, &sw_mac[0], &sw_mac[1], &sw_mac[2], &sw_mac[3], &sw_mac[4], &sw_mac[5], relay, sw_ip, &eth, &port, &vlan) == 24) {

	snprintf(str, len, "%s %2d %s %s %s BIND for HOSTMAC %02x:%02x:%02x:%02x:%02x:%02x LEASED-IP %s "
	    "HOSTNAME %s SW-MAC %02x:%02x:%02x:%02x:%02x:%02x RELAY %s Circuit-ID (%s eth %d/%d:%d)",
	    mon, day, time, host, proc, hostmac[0], hostmac[1], hostmac[2], hostmac[3], hostmac[4], hostmac[5], leased_ip,
	    hostname, sw_mac[0], sw_mac[1], sw_mac[2], sw_mac[3], sw_mac[4], sw_mac[5], relay, sw_ip, eth, port, vlan);
    }
}

/* Remove \n from line */
static char *chomp(char * const str) {
    size_t len = strlen(str);

    if (str[len - 1] == '\n')
	str[len - 1] = '\0';

    /* Remove <190> at begin */
    memmove(str, str + 5, len - 4);

    return str;
}

int main(int argc, char *argv[]) {
    struct sockaddr_in myself;
    char buffer[8192];
    char sql_server[64];
    char sql_username[64];
    char sql_password[64];
    char sql_database[64];
    char log_file[128];
    char val[64];
    int iostream;
    int sockfd;
    int sql_port;
    int listen_port;
    FILE *fp;
    pcre *re1;
    pcre *re2;
    const char *error;
    int erroffset;
    MYSQL dbh;

    if (fork())
	exit(EXIT_SUCCESS);

    setsid();
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    iostream = open(_PATH_DEVNULL, O_RDWR);
    dup(iostream);
    dup(iostream);
    openlog(PROG_NAME, LOG_PID, LOG_DAEMON);

    sql_server[0] = '\0';
    sql_username[0] = '\0';
    sql_password[0] = '\0';
    sql_database[0] = '\0';
    sql_port = 3306;
    listen_port = 0;

    /* Read options */
    if ((fp = fopen(CONFIG_FILE, "r"))) {
        while (fgets(buffer, sizeof(buffer), fp)) {
            if ((buffer[0] == '#') || (buffer[0] == '\n')) {
                continue;
            } else if ((sscanf(buffer, "server: %63[A-Za-z0-9 .:/_+-]", val)) == 1) {
                strcpy(sql_server, val);
                continue;
            } else if ((sscanf(buffer, "username: %63[A-Za-z0-9 .:/_+-]", val)) == 1) {
                strcpy(sql_username, val);
                continue;
            } else if ((sscanf(buffer, "password: %63[A-Za-z0-9 .:/_+-]", val)) == 1) {
                strcpy(sql_password, val);
                continue;
            } else if ((sscanf(buffer, "database: %63[A-Za-z0-9 .:/_+-]", val)) == 1) {
                strcpy(sql_database, val);
                continue;
            } else if ((sscanf(buffer, "logfile: %127[A-Za-z0-9 .:/_+-]", val)) == 1) {
                strcpy(log_file, val);
                continue;
            } else if ((sscanf(buffer, "port: %63[A-Za-z0-9 .:/_+-]", val)) == 1) {
                sql_port = atoi(val);
                continue;
            } else if ((sscanf(buffer, "listen: %63[A-Za-z0-9 .:/_+-]", val)) == 1) {
                listen_port = atoi(val);
                continue;
            }
        }
        fclose(fp);
    }

    /* Required options */
    if (!sql_server[0]) {
        syslog(LOG_ERR, "ERROR: Option `server' is not set in %s", CONFIG_FILE);
        exit(EXIT_FAILURE);
    } else if (!sql_username[0]) {
        syslog(LOG_ERR, "ERROR: Option `username' is not set in %s", CONFIG_FILE);
        exit(EXIT_FAILURE);
    } else if (!sql_password[0]) {
        syslog(LOG_ERR, "ERROR: Option `password' is not set in %s", CONFIG_FILE);
        exit(EXIT_FAILURE);
    } else if (!sql_database[0]) {
        syslog(LOG_ERR, "ERROR: Option `database' is not set in %s", CONFIG_FILE);
        exit(EXIT_FAILURE);
    } else if (!listen_port) {
        syslog(LOG_ERR, "ERROR: Option `listen' is not set in %s", CONFIG_FILE);
        exit(EXIT_FAILURE);
    }

    memset(buffer, '\0', sizeof(buffer));

    re1 = pcre_compile("(\\d{2}:\\d{2}:\\d{2}) \\S+ \\S+: BIND for HOSTMAC ((?:[0-9a-f]{1,2}:){5}[0-9a-f]{1,2}) LEASED-IP "
	"(\\d{1,3}.\\d{1,3}.\\d{1,3}.\\d{1,3}) HOSTNAME (\\S+) SW-MAC ((?:[0-9a-f]{1,2}:){5}[0-9a-f]{1,2}) RELAY "
	"(\\d{1,3}.\\d{1,3}.\\d{1,3}.\\d{1,3}) Circuit-ID \\(((?:\\d{1,3}.?){4})\\seth\\s\\d?\\/(\\d{1,2}):(\\d{1,5})\\).*DHCPREQUEST.*from \\2",
	PCRE_CASELESS, &error, &erroffset, NULL);

    re2 = pcre_compile("(\\d{2}:\\d{2}:\\d{2}) \\S+ \\S+: BIND for HOSTMAC ((?:[0-9a-f]{1,2}:){5}[0-9a-f]{1,2}) LEASED-IP "
	"(\\d{1,3}.\\d{1,3}.\\d{1,3}.\\d{1,3}) HOSTNAME (\\S+) SW-MAC ((?:[0-9a-f]{1,2}:){5}[0-9a-f]{1,2}) RELAY "
	"(\\d{1,3}.\\d{1,3}.\\d{1,3}.\\d{1,3}) Circuit-ID \\(((?:\\d{1,3}.?){4})\\seth\\s\\d?\\/(\\d{1,2}):(\\d{1,5})\\)",
	PCRE_CASELESS, &error, &erroffset, NULL);

    if ((sockfd = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
	syslog(LOG_ERR, "ERROR: Opening DGRAM socket");
	exit(EXIT_FAILURE);
    }

    memset(&myself, 0, sizeof(myself));
    myself.sin_family = AF_INET;
    myself.sin_port = htons(listen_port);
    myself.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sockfd, (struct sockaddr *) &myself, sizeof(myself))) {
	syslog(LOG_ERR, "ERROR: Binding to UDP port %d", listen_port);
	exit(EXIT_FAILURE);
    }

    if ((fp = fopen(PID_FILE, "w"))) {
	fprintf(fp, "%u\n", getpid());
	fclose(fp);
    }

    if (log_file[0]) {
	if (!(fp = fopen(log_file, "a"))) {
	    syslog(LOG_ERR, "ERROR: Opening log file %s", log_file);
	    exit(EXIT_FAILURE);
	}
	setbuf(fp, NULL);
    }

    if (!mysql_init(&dbh)) {
        syslog(LOG_ERR, "%s", mysql_error(&dbh));
        exit(EXIT_FAILURE);
    }

    my_bool opt = (my_bool) 1;
    mysql_options(&dbh, MYSQL_OPT_RECONNECT, &opt);
    mysql_options(&dbh, MYSQL_INIT_COMMAND, "SET NAMES 'CP866'");

    while (!mysql_real_connect(&dbh, sql_server, sql_username, sql_password, sql_database, sql_port, NULL, 0)) {
        syslog(LOG_WARNING, "%s", mysql_error(&dbh));
        sleep(5);
    }

    syslog(LOG_INFO, "Successfully connected to MySQL database");

    while (1) {
	char tmp[1024];
	char query[1024];
	int ovector[32];
	char date[16];
	char cur_time[16];
	char host_mac[32];
	char leased_ip[16];
	char hostname[128];
	char hostname_esc[256];
	char relay[16];
	char sw_mac[32];
	char sw_ip[16];
	char port[8];
	char vlan[8];
	time_t t;
	int ack = EOF;
	size_t len;

	memset(tmp, '\0', sizeof(tmp));
	memset(date, '\0', sizeof(date));
	memset(cur_time, '\0', sizeof(cur_time));
	memset(host_mac, '\0', sizeof(host_mac));
	memset(leased_ip, '\0', sizeof(leased_ip));
	memset(hostname, '\0', sizeof(hostname));
	memset(relay, '\0', sizeof(relay));
	memset(sw_mac, '\0', sizeof(sw_mac));
	memset(sw_ip, '\0', sizeof(sw_ip));
	memset(port, '\0', sizeof(port));
	memset(vlan, '\0', sizeof(vlan));

	recvfrom(sockfd, tmp, sizeof(tmp), 0, NULL, NULL);
	chomp(tmp);

	if (strstr(tmp, "BIND for HOSTMAC")) {
	    normalize_log(tmp, sizeof(tmp));
	    
	    t = time(NULL);
	    strftime(date, sizeof(date), "%Y-%m-%d", localtime(&t));

	    if (pcre_exec(re1, NULL, (char *) buffer, strlen(buffer), 0, 0, ovector, 32) > 0) {
		ack = 1;
	    } else if (pcre_exec(re2, NULL, (char *) buffer, strlen(buffer), 0, 0, ovector, 32) > 0) {
		ack = 0;
	    }

	    if (ack != EOF) {
		strncpy(cur_time, &buffer[ovector[2]], ovector[3] - ovector[2]);
		strncpy(host_mac, &buffer[ovector[4]], ovector[5] - ovector[4]);
		strncpy(leased_ip, &buffer[ovector[6]], ovector[7] - ovector[6]);
		strncpy(hostname, &buffer[ovector[8]], ovector[9] - ovector[8]);
		strncpy(sw_mac, &buffer[ovector[10]], ovector[11] - ovector[10]);
		strncpy(relay, &buffer[ovector[12]], ovector[13] - ovector[12]);
		strncpy(sw_ip, &buffer[ovector[14]], ovector[15] - ovector[14]);
		strncpy(port, &buffer[ovector[16]], ovector[17] - ovector[16]);
		strncpy(vlan, &buffer[ovector[18]], ovector[19] - ovector[18]);

		/* Escape hostname value */
		mysql_real_escape_string(&dbh, hostname_esc, hostname, strlen(hostname));

		len = snprintf(query, sizeof(query), "INSERT INTO bindlog (date, time, host_mac, leased_ip, hostname, relay, sw_mac, sw_ip, port, vlan, ack)"
		    " VALUES ('%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%d')",
		    date, cur_time, host_mac, leased_ip, hostname_esc, relay, sw_mac, sw_ip, port, vlan, ack);

		if (mysql_real_query(&dbh, query, len)) {
		    syslog(LOG_WARNING, "%s", mysql_error(&dbh));
		}
	    }
	    strncpy(buffer, tmp, sizeof(buffer));
	} else {
	    strncat(buffer, tmp, sizeof(buffer));
	}

	if (log_file[0]) {
	    fprintf(fp, "%s\n", tmp);
	}
    }
/*
    close(sockfd);
    closelog();
    fclose(logfd);
*/
    return 0;
}
