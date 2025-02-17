/*
 * List - the cool GoboLinux' "List" shell script, written in C
 *
 * Copyright (C) 2004-2005 Lucas Correia Villa Real <lucasvr@gobolinux.org>
 *
 * Released under the GNU GPL.
 */
#include <stdio.h>
#include <libgen.h>
#include <limits.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <getopt.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#define __USE_LARGEFILE64
#define __USE_FILE_OFFSET64
#include <sys/stat.h>
#ifdef __APPLE__
   #include <sys/param.h>
   #include <sys/mount.h>
   #define MAJOR(x) (long long) major(x)
   #define MINOR(x) (long long) minor(x)
#elif defined(__FreeBSD__)
   #include <sys/mount.h>
   #define MAJOR(x) (long long) major(x)
   #define MINOR(x) (long long) minor(x)
#else
   #include <sys/statfs.h>
   #include <sys/vfs.h>
   #if defined (__CYGWIN__)
      #define MAJOR(dev)      (long long) ((dev)>>8)
      #define MINOR(dev)      (long long) ((dev) & 0xff)
      #define MKDEV(ma,mi)    (long long) ((ma)<<8 | (mi))
   #else
      #include <linux/kdev_t.h>
   #endif
#endif
#include "List.h"

char default_ls_colors[] =
   "no=00:fi=40;33:di=01;34:ln=01;36:pi=40;33:so=01;35:do=01;35:bd=40;33;01:"
   "cd=40;33;01:or=40;31;01:ex=01;32:*.tar=01;31:*.tgz=01;31:*.arj=01;31:"
   "*.taz=01;31:*.lzh=01;31:*.zip=01;31:*.z=01;31:*.Z=01;31:*.gz=01;31:"
   "*.bz2=01;31:*.deb=01;31:*.rpm=01;31:*.jar=01;31:*.jpg=01;35:"
   "*.jpeg=01;35:*.gif=01;35:*.bmp=01;35:*.pbm=01;35:*.pgm=01;35:*.ppm=01;35:"
   "*.tga=01;35:*.xbm=01;35:*.xpm=01;35:*.tif=01;35:*.tiff=01;35:*.png=01;35:"
   "*.mpg=01;35:*.mpeg=01;35:*.avi=01;35:*.fli=01;35:*.gl=01;35:*.dl=01;35:"
   "*.xcf=01;35:*.xwd=01;35:*.ogg=01;35:*.mp3=01;35:*.wav=01;35:";

#define COLOR_WHITE_CODE       "\033[0m"
#define COLOR_GREY_CODE        "\033[01;30m"
#define COLOR_GREEN_CODE       "\033[22;36m"
#define COLOR_YELLOW_CODE      "\033[1;33m"
#define COLOR_LIGHT_WHITE_CODE "\033[1;37m" 
#define COLOR_LIGHT_GREEN_CODE "\033[1;36m" 
#define COLORCODE_MAX     16
#define EXTENSION_MAX      8
#define COLOR_MAXENTRIES 30000

struct color_list {
   char  extension[EXTENSION_MAX];
   char  code[COLORCODE_MAX];
};

static int color_list_len;
struct color_list *colors;
struct color_list normal_color;

struct file_info {
    struct stat status;
    char *full_pathname;
};
    
struct fs_info {
    int magic;
    char *name;
};

static int opt_all = 0;
static int opt_dir = 0;
static int opt_hid = 0;
static int opt_nolink = 0;
static int opt_size = 0;
static int opt_time = 0;
static int opt_help = 0;

static char *current_dir;

const char shortopts[] = "adhLzts";
static struct option long_options[] = {
    {"all",         0, &opt_all, 1},
    {"directories", 0, &opt_dir, 1},
    {"help",        0, &opt_help, 1},
    {"hidden",      0, &opt_hid, 1},   /* -d .* */
    {"no-links",    0, &opt_nolink, 1},
    {"size",        0, &opt_size, 1},  /* --sort=size -r */
    {"time",        0, &opt_time, 1},  /* --sort=time -r */
    {0, 0, 0, 0}
};

int
is_hidden(char *filename)
{
   char *base = basename(filename);
   if (!strcmp(base, "..") || !strcmp(base, "."))
      return 0;
   return (base[0] == '.' ? 1 : 0);
}

int
is_mygroup(gid_t gid)
{
   gid_t groups[NGROUPS_MAX];
   unsigned int i;
   unsigned int count;

   if (gid == getegid())
      return 1;

   if ((count = getgroups(NGROUPS_MAX, groups)) == -1) {
      perror("getgroups");
      return 0;
   }

   for (i=0; i < count; i++) {
      if (gid == groups[i])
         return 1;
   }
   return 0;
}

void 
set_color(char *ext, char *code, int index)
{
   memset(colors[index].extension, 0, sizeof(colors[index].extension)); 
   memset(colors[index].code, 0, sizeof(colors[index].code)); 
   snprintf(colors[index].extension, sizeof(colors[index].extension), "%s", ext); 
   snprintf(colors[index].code, sizeof(colors[index].code), "%s", code);

   if (! strcmp(ext, "no"))
      memcpy(&normal_color, &colors[index], sizeof(struct color_list));
}

void
set_dircolors()
{
   int i;
   char *env = NULL, *env_copy = NULL, *token, *equal_char;
   
   env = getenv("LS_COLORS");
   if (! env || env[0] == '\0') {
      /* use default colors */
      env = default_ls_colors;
   }
   
   env_copy = strdup(env);
   env = strdup(env_copy);
   if (! env_copy || ! env) {
      perror("strdup");
      goto fail;
   }

   /* 
    * count number of entries in environment variable
    * $LS_COLORS; a hardcoded maximum of entries
    * (COLOR_MAXENTRIES) exists as a safety precaution. 
    */
   color_list_len = 1;
   token = strtok(env_copy, ":");
   while ((token = strtok(NULL, ":")))
      if (++color_list_len >= COLOR_MAXENTRIES)
         break;

   colors = (struct color_list *) malloc(sizeof(struct color_list) * color_list_len);
   if (! colors) {
      perror("malloc");
      goto fail;
   }
   
   for (i = 0; i < color_list_len; ++i) {
      token = strtok((i == 0 ? env : NULL), ":");
      if (! token) {
         perror("strtok");
         break;
      }
      if (token[0] == '*')
         token++;
      
      equal_char = strstr(token, "=");
      *equal_char = '\0';

      /* 'token' now has the extension, with its color code in 'equal_char+1' */
      set_color(token, equal_char+1, i); 
   }

   if (i < color_list_len) {
      /* couldn't parse all colors for some reason; truncate to 'i' elements */
      void *tmp = realloc(colors, sizeof(struct color_list) * i);
      if (tmp)
         colors = (struct color_list *) tmp;
      color_list_len = i;
   }

cleanup:
   if (env)
      free(env);
   if (env_copy)
      free(env_copy);
   return;

fail:
   color_list_len = 0;
   goto cleanup;
}

void
get_file_extension(char *filename, char *extension, int len)
{
   char *file_copy, *dot, *dot_tmp;
   
   memset(extension, 0, len);
   if (! strstr(filename, "."))
      return;

   /* find the last '.' character in the string */
   file_copy = strdup(filename);
   dot = strtok(file_copy, ".");
   if (! dot) {
      free(file_copy);
      return;
   }

   do {
      dot_tmp = dot;
      dot = strtok(NULL, ".");
      if (! dot) {
         break;
      }
   } while(dot);
   dot = dot_tmp;

   snprintf(extension, len, ".%s", dot);
   free(file_copy);
}

void
get_file_color(char *full_pathname, char *extension, char *color, int len, mode_t st_mode)
{
   int i;
   char needle[len];
   
   memset(color, 0, len);

   if (S_ISSOCK(st_mode))
      snprintf(needle, len, "so");
   
   else if (S_ISFIFO(st_mode))
      snprintf(needle, len, "fi");
   
   else if (S_ISLNK(st_mode)) {
      char tmp_buffer[PATH_MAX], symlink_path[PATH_MAX];
      struct stat target_status;
      ssize_t n;

      n = readlink(full_pathname, tmp_buffer, sizeof(tmp_buffer));
      if (n < 0) {
         perror("readlink");
         return;
      }
      tmp_buffer[n] = '\0';
      realpath(full_pathname, symlink_path);
      if (lstat(symlink_path, &target_status) < 0)
         snprintf(needle, len, "or");
      else
         snprintf(needle, len, "ln");
      
   }

   else if (S_ISDIR(st_mode))
      snprintf(needle, len, "di");
   
   else if (S_ISCHR(st_mode))
      snprintf(needle, len, "cd");
   
   else if (S_ISBLK(st_mode))
      snprintf(needle, len, "bd");

   else if (S_ISREG(st_mode)) {
      /* check if the file is executable */
      if (st_mode & S_IXUSR || st_mode & S_IXGRP || st_mode & S_IXOTH)
         snprintf(needle, len, "ex");
      else
         snprintf(needle, len, "%s", extension);
   }
   
   else {
      /* error? let's use the standard (normal) color... */
      snprintf(color, len, "%s", normal_color.code);
      return;
   }

   /* search for the pattern on the list */
   for (i = 0; i < color_list_len; ++i) {
      if (! strcmp(colors[i].extension, needle)) {
         snprintf(color, len, "%s", colors[i].code);
         return;
      }
   }

   /* oh well.. we need to return the normal color in this case */
   snprintf(color, len, "%s", normal_color.code);
}

#define SCHEME_STATUS 1
#define SCHEME_FILES  2
char *
colorize_bytes(unsigned long long value, int color_scheme, int pad_bytes)
{
    int len;
    char *color_3 = NULL, *color_6 = NULL, *color_start = NULL;
    char tmp_buf[64], buf[64], *ptr_3, *ptr_6, *ptr_start;

    sprintf(tmp_buf, "%lld", value);
    len = strlen(tmp_buf);
    
    ptr_start = tmp_buf;       /* XXXXXyyyzzz */
    ptr_6 = &tmp_buf[len-6];   /* xxxxxYYYzzz */
    ptr_3 = &tmp_buf[len-3];   /* xxxxxyyyZZZ */

    if (color_scheme == SCHEME_STATUS) {
        color_start = COLOR_LIGHT_WHITE_CODE;
        color_6     = COLOR_LIGHT_GREEN_CODE;
        color_3     = COLOR_GREEN_CODE;
        
    } else if (color_scheme == SCHEME_FILES) {
        color_start = COLOR_LIGHT_GREEN_CODE;
        color_6     = COLOR_GREEN_CODE;
        color_3     = COLOR_WHITE_CODE;
    }

    memset(buf, 0, sizeof(buf));

    if (len > 6) {
        sprintf(buf, "%s", color_start);
        strncat(buf, ptr_start, len - 6);
        
        strncat(buf, color_6, strlen(color_6));
        strncat(buf, ptr_6, 3);
        
        strncat(buf, color_3, strlen(color_3));
        strncat(buf, ptr_3, 3);
    } else if (len > 3) {
        sprintf(buf, "%s", color_6);
        strncat(buf, ptr_start, len - 3);
        
        strncat(buf, color_3, strlen(color_3));
        strncat(buf, ptr_3, 3);
    } else {
        sprintf(buf, "%s", color_3);
        strncat(buf, ptr_start, len);
    }

    if (pad_bytes && len < pad_bytes) {
        int i, j;
        int skip_bytes = pad_bytes - len;
        
        memcpy(tmp_buf, buf, sizeof(buf));
        for (i=0; i<skip_bytes; ++i)
            buf[i] = ' ';
        
        for (j=0; j<strlen(tmp_buf); ++j)
            buf[i++] = tmp_buf[j];
    }
    return strdup(buf);
}

/* format the permission mask */
void
set_permission_string(struct stat *status, char *mask_U, char *mask_G, char *mask_O, int len, char *final_mask, int mask_len)
{
   memset(mask_U, '-', len);
   memset(mask_G, '-', len);
   memset(mask_O, '-', len);
   mask_U[3] = '\0';
   mask_G[3] = '\0';
   mask_O[3] = '\0';

   if (status->st_mode & S_IRUSR)
      mask_U[0] = 'r';
   if (status->st_mode & S_IWUSR)
      mask_U[1] = 'w';
   if (status->st_mode & S_ISUID)
      mask_U[2] = 's';
   else if (status->st_mode & S_IXUSR)
      mask_U[2] = 'x';

   if (status->st_mode & S_IRGRP)
      mask_G[0] = 'r';
   if (status->st_mode & S_IWGRP)
      mask_G[1] = 'w';
   if (status->st_mode & S_ISGID)
      mask_G[2] = 's';
   else if (status->st_mode & S_IXGRP)
      mask_G[2] = 'x';

   if (status->st_mode & S_IROTH)
      mask_O[0] = 'r';
   if (status->st_mode & S_IWOTH)
      mask_O[1] = 'w';
   if (status->st_mode & S_ISVTX)
      mask_O[2] = 't';
   else if (status->st_mode & S_IXOTH)
      mask_O[2] = 'x';
#if 0
   if (S_ISSOCK(status->st_mode))
      mask_U[0] = 's';
   else if (S_ISFIFO(status->st_mode))
      mask_U[0] = 'p';
#endif
   memset(final_mask, 0, mask_len);
   if (status->st_uid == geteuid())
      snprintf(final_mask, mask_len, "%s%s%s%s%s", 
          COLOR_WHITE_CODE, mask_U, COLOR_GREY_CODE, mask_G, mask_O);
   
   else if (is_mygroup(status->st_gid))
      snprintf(final_mask, mask_len, "%s%s%s%s%s%s", 
          COLOR_GREY_CODE, mask_U, COLOR_WHITE_CODE, mask_G, COLOR_GREY_CODE, mask_O);

   else
      snprintf(final_mask, mask_len, "%s%s%s%s%s", 
          COLOR_GREY_CODE, mask_U, mask_G, COLOR_WHITE_CODE, mask_O);
}

void
really_list_entries(struct file_info *file_info, struct dirent **namelist, int stat_num, 
                    long long *total, long *counter, long *hiddenfiles)
{
   int i, pass;
   char mask_U[4], mask_G[4], mask_O[4], final_mask[64];
   char tmp_buffer[PATH_MAX], symlink_path[PATH_MAX], link_entry[PATH_MAX * 2];
   char extension[EXTENSION_MAX], color_code[COLORCODE_MAX];
   char *full_pathname;
   struct tm *time_info;
   struct stat status, target_status;

   for (pass = 0; pass < 7; ++pass) {
      if (opt_time || opt_size)
         pass = 99;
      
      for (i = 0; i < stat_num; ++i) {
         if (namelist[i] == NULL)
            continue;
         
         if (namelist[i]->d_name[0] == '.' && !opt_all && !opt_hid) {
            if (is_hidden(namelist[i]->d_name)) {
               if (pass == 0 || pass == 99)
                  *hiddenfiles += 1;
               continue;
            }

            if (! strcmp(namelist[i]->d_name, "..") ||
               ! strcmp(namelist[i]->d_name, "."))
               continue;
         }

         if (opt_hid && !is_hidden(namelist[i]->d_name))
            continue;

         memset(link_entry, 0, sizeof(link_entry));
            status = file_info[i].status;
            full_pathname = file_info[i].full_pathname;
         
         if (opt_dir && !S_ISDIR(status.st_mode))
            continue;
            
         switch (pass) {
            case 0:
               if (!S_ISSOCK(status.st_mode))
                  continue;
               break;

            case 1:
               if (!S_ISFIFO(status.st_mode))
                  continue;
               break;

            case 2:
               if ((!S_ISLNK(status.st_mode) || opt_nolink))
                  continue;
               break;

            case 3:
               if (! S_ISDIR(status.st_mode))
                  continue;
               break;

            case 4:
               if (! S_ISREG(status.st_mode))
                  continue;
               break;

            case 5:
               if (! S_ISCHR(status.st_mode))
                  continue;
               break;

            case 6:
               if (! S_ISBLK(status.st_mode))
                  continue;
               break;

            default:
               break;
         }

         if ((S_ISLNK(status.st_mode) && !opt_nolink)) {
            size_t n = readlink(full_pathname, tmp_buffer, sizeof(tmp_buffer));
            if (n < 0) {
               perror("readlink");
               continue;
            }
            tmp_buffer[n] = '\0';
            realpath(full_pathname, symlink_path);
            lstat(symlink_path, &target_status);

            get_file_extension(symlink_path, extension, sizeof(extension));
            get_file_color(symlink_path, extension, color_code, sizeof(color_code), target_status.st_mode);
            snprintf(link_entry, sizeof(link_entry), "%s -> \033[%sm%s", COLOR_WHITE_CODE, color_code, tmp_buffer);
         }
            
         get_file_extension(namelist[i]->d_name, extension, sizeof(extension));
         get_file_color(full_pathname, extension, color_code, sizeof(color_code), status.st_mode);
         set_permission_string(&status, mask_U, mask_G, mask_O, sizeof(mask_U), final_mask, sizeof(final_mask));
         
         /* get date/time info from file */
         time_info = localtime((const time_t *) &status.st_mtime);
         if (! time_info) {
            perror("localtime");
            continue;
         }

         if (S_ISCHR(status.st_mode) || S_ISBLK(status.st_mode)) {
            char major_minor[64], tmp[64];
            memset(major_minor, 0, sizeof(major_minor));
            sprintf(major_minor, "%" PRIu64 ":%-" PRIu64, MAJOR(status.st_rdev), MINOR(status.st_rdev));
            if (strlen(major_minor) < 7) {
               size_t padding = 7 - strlen(major_minor);
               memset(tmp, 0, sizeof(tmp));
               for (size_t i=0; i<padding; ++i)
                  tmp[i] = ' ';
               strncat(tmp, major_minor, sizeof(tmp)-padding-1);
               sprintf(major_minor, tmp);
            }
            fprintf(stdout, "%s%02d/%02d %02d:%02d %s%s    %s\033[%sm %s\n",
               COLOR_WHITE_CODE,
               time_info->tm_mday,
               time_info->tm_mon + 1,
               time_info->tm_hour,
               time_info->tm_min,
               final_mask,
               COLOR_WHITE_CODE,
               major_minor,
               color_code,
               namelist[i]->d_name);
         } else {
            char *size_str = colorize_bytes(status.st_size, SCHEME_FILES, 11);

            fprintf(stdout, "%s%02d/%02d %02d:%02d %s%s%s \033[%sm%s%s\n",
               COLOR_WHITE_CODE,
               time_info->tm_mday,
               time_info->tm_mon + 1,
               time_info->tm_hour,
               time_info->tm_min,
               final_mask,
               COLOR_WHITE_CODE,
               size_str,
               color_code,
               namelist[i]->d_name,
               link_entry);
         }
      
         *counter += 1;
         *total   += status.st_size;
      }
   }
}

#define RETURN_SORT(a, b) do { \
   if (a < b) { \
      return -1; \
   } else if (a == b) { \
      return 0; \
   } else { \
      return 1; \
   } \
} while(0)


int
time_sort(const struct dirent **a, const struct dirent **b)
{
   struct stat status_a, status_b;
   char filename_a[PATH_MAX];
   char filename_b[PATH_MAX];

   sprintf(filename_a, "%s/%s", current_dir, (*a)->d_name);
   sprintf(filename_b, "%s/%s", current_dir, (*b)->d_name);
   stat(filename_a, &status_a);
   stat(filename_b, &status_b);

#if _POSIX_C_SOURCE >= 200809L
   if (status_a.st_mtim.tv_sec == status_b.st_mtim.tv_sec) {
      RETURN_SORT(status_a.st_mtim.tv_nsec, status_b.st_mtim.tv_nsec);
   } else {
      RETURN_SORT(status_a.st_mtime, status_b.st_mtime);
   }
#else
   RETURN_SORT(status_a.st_mtime, status_b.st_mtime);
#endif
}

int
size_sort(const struct dirent **a, const struct dirent **b)
{
   struct stat status_a, status_b;
   char filename_a[PATH_MAX];
   char filename_b[PATH_MAX];

   sprintf(filename_a, "%s/%s", current_dir, (*a)->d_name);
   sprintf(filename_b, "%s/%s", current_dir, (*b)->d_name);
   stat(filename_a, &status_a);
   stat(filename_b, &status_b);
   RETURN_SORT(status_a.st_size, status_b.st_size);
}

int
list_file(const char *path, long long *total, long *counter, long *hiddenfiles)
{
    int ret, n;
    struct stat status;
    struct file_info file_info;
    struct dirent **namelist;
    
    ret = lstat(path, &status);
    if (ret < 0)
        return -1;

    if (!S_ISSOCK(status.st_mode) && !S_ISFIFO(status.st_mode) && !S_ISLNK(status.st_mode)
            && !S_ISDIR(status.st_mode) && !S_ISREG(status.st_mode) && !S_ISCHR(status.st_mode)
            && !S_ISBLK(status.st_mode)) {
        return -1;
   }

    n = 1;
    namelist = (struct dirent **) malloc(sizeof(struct dirent *));
    if (! namelist) {
        perror("malloc");
        return -1;
    }
    namelist[0] = (struct dirent *) malloc(sizeof(struct dirent));
    if (! namelist[0]) {
        perror("malloc");
        free(namelist);
        return -1;
    }
    snprintf(namelist[0]->d_name, sizeof(namelist[0]->d_name), "%s", path);

    file_info.status = status;
    file_info.full_pathname = namelist[0]->d_name;
    really_list_entries(&file_info, namelist, n, total, counter, hiddenfiles);

    free(namelist[0]);
    free(namelist);

   return 0;
}

int
list_entries(const char *path, long long *total, long *counter, long *hiddenfiles, bool isbrokenlink)
{
   int i, n, ret, len;
   char complete_path[PATH_MAX];
   struct dirent **namelist;
   struct file_info *file_info;
   
   /* scandir doesn't propagate the complete pathname */
   if (isbrokenlink)
      strncpy(complete_path, path, sizeof(complete_path)-1);
   else if (realpath(path, complete_path) == NULL) {
      perror(path);
      return -1;
   }
   current_dir = complete_path;
   
   if (opt_time) {
      /* sort the list with the oldest file in the head */
      n = scandir(path, &namelist, NULL, time_sort);
   } else if (opt_size) {
      /* sort the list with the smallest file in the head */
      n = scandir(path, &namelist, NULL, size_sort);
   } else {
      /* alpha sort */
      n = scandir(path, &namelist, NULL, alphasort);
   }
    if (n < 0)
        return list_file(path, total, counter, hiddenfiles);

    file_info = (struct file_info *) calloc(n, sizeof(struct file_info));
    if (! file_info) {
        perror("calloc");
        return -1;
    }
    
    for (i=0; i<n; ++i) {
        /* fill full_pathaname string */
        len = (strlen(path) + strlen(namelist[i]->d_name) + 2);
        file_info[i].full_pathname = (char *) malloc(sizeof(char) * len);
        if (! strlen(path))
            sprintf(file_info[i].full_pathname, "%s", namelist[i]->d_name);
        else
            sprintf(file_info[i].full_pathname, "%s/%s", path, namelist[i]->d_name);

        /* cache that! */
        ret = lstat(file_info[i].full_pathname, &file_info[i].status);
        if (ret < 0) {
            fprintf(stderr, "lstat %s: %s\n", file_info[i].full_pathname, strerror(errno));
            continue;
        }
   }

    really_list_entries(file_info, namelist, n, total, counter, hiddenfiles);
   
   for (i=0; i<n; ++i) {
      free(namelist[i]);
        if (file_info[i].full_pathname)
            free(file_info[i].full_pathname);
    }
   free(namelist);
    free(file_info);

   return 0;
}

void
usage(char *program_name)
{
   fprintf(stderr,
         "%s: List information about files and directories.\n\n"
         "Usage:\n\n"
         "    List [<options>...] <pattern>\n\n"
         "-a, --all         \n"
         "    List both regular and dot-files.\n"
         "-d, --directories \n"
         "    List directories only.\n"
         "-h, --hidden      \n"
         "    List dot-files only.\n"
         "-L, --no-links    \n"
         "    List only files that are not symbolic links.\n"
         "-s, --size        \n"
         "    Sort by size, largest size shown last.\n"
         "-t, --time        \n"
         "    Sort by time, most recent file shown last.\n\n",
         program_name);

    exit(EXIT_FAILURE);
}

char const *
get_filesystem(struct statfs *status)
{
#ifndef __FreeBSD__
    int i;
    struct fs_info fs_info[] = {
        { ADFS_SUPER_MAGIC,     "adfs" },
        { AFFS_SUPER_MAGIC,     "affs" },
        { BEFS_SUPER_MAGIC,     "befs" },
        { BFS_MAGIC,            "bfs"  },
        { CIFS_MAGIC_NUMBER,    "cifs" },
        { CODA_SUPER_MAGIC,     "coda" },
        { COH_SUPER_MAGIC,      "coh"  },
        { CRAMFS_MAGIC,         "cramfs" },
        { DEVFS_SUPER_MAGIC,    "devfs"  },
        { EFS_SUPER_MAGIC,      "efs" },
        { EXT_SUPER_MAGIC,      "ext" },
        { EXT2_OLD_SUPER_MAGIC, "ext2" },
        { EXT2_SUPER_MAGIC,     "ext2" },
        { EXT3_SUPER_MAGIC,     "ext3" },
        { HFS_SUPER_MAGIC,      "hfs"  },
        { HPFS_SUPER_MAGIC,     "hpfs" },
        { HUGETLBFS_MAGIC,      "hugetlbfs" },
        { ISOFS_SUPER_MAGIC,    "isofs" },
        { JFFS2_SUPER_MAGIC,    "jffs2" },
        { JFS_SUPER_MAGIC,      "jsf" },
        { MINIX_SUPER_MAGIC,    "minix" },
        { MINIX_SUPER_MAGIC2,   "minix" },
        { MINIX2_SUPER_MAGIC,   "minix2" },
        { MINIX2_SUPER_MAGIC2,  "minix2" },
        { MSDOS_SUPER_MAGIC,    "msdos" },
        { NCP_SUPER_MAGIC,      "ncp" },
        { NFS_SUPER_MAGIC,      "nfs" },
        { NTFS_SB_MAGIC,        "ntfs" },
        { OPENPROM_SUPER_MAGIC, "openprom" },
        { PROC_SUPER_MAGIC,     "proc" },
        { QNX4_SUPER_MAGIC,     "qnx4" },
        { REISERFS_SUPER_MAGIC, "reiserfs" },
        { ROMFS_MAGIC,          "romfs" },
        { SMB_SUPER_MAGIC,      "smb" },
        { SYSV2_SUPER_MAGIC,    "sysv2" },
        { SYSV4_SUPER_MAGIC,    "sysv4" },
        { TMPFS_MAGIC,          "tmpfs" },
        { UDF_SUPER_MAGIC,      "udf" },
        { UFS_MAGIC,            "ufs" },
        { USBDEVICE_SUPER_MAGIC,"usbdevice" },
        { VXFS_SUPER_MAGIC,     "vxfs" },
        { XENIX_SUPER_MAGIC,    "xenix" },
        { XFS_SUPER_MAGIC,      "xfs" },
        { _XIAFS_SUPER_MAGIC,   "xiafs" },
        { RAMFS_MAGIC,          "ramfs" },
        { SYSFS_MAGIC,          "sysfs" },
        { DEVPTS_MAGIC,         "devpts" },
    };

    for (i = 0; i < sizeof(fs_info)/sizeof(struct fs_info); ++i) {
        if (status->f_type == fs_info[i].magic)
            return fs_info[i].name;
    }
    return "filesystem";
#else /* __FreeBSD__ */
   /* 
    * Those magic numbers don't work on FreeBSD. OTOH, the statfs struct
    * on that platform has a convenient filesystem name in cleartext.
    */
   return status->f_fstypename;
#endif
}

void
summarize_simple(long long total, long counter, long hiddenfiles)
{
   char *bytes_total_string = colorize_bytes(total, SCHEME_FILES, 11);

   printf("%s                      ---------\n", COLOR_WHITE_CODE);

   if (hiddenfiles)
      printf("                      %s in %ld%s+%ld%s files\n", bytes_total_string, 
            counter, COLOR_GREY_CODE, hiddenfiles, COLOR_WHITE_CODE);
   else
      printf("                      %s in %ld files\n", bytes_total_string, counter);
}

void
summarize(struct statfs status, long long total, long counter, long hiddenfiles, int final_info)
{
    char *bytes_used_string, *bytes_free_string, *bytes_total_string;
   static int cols = 0;
   static long bytes_used = 0, bytes_free = 0, bytes_total = 0;
   static float percent = 0.0;
   struct winsize w;
   int i;
   
   /* let's use a sane output */
   for (i = 10; i; --i) {
      if (status.f_bsize > 1)
         status.f_bsize >>= 1;
      else {
         status.f_blocks >>= 1;
         status.f_bfree  >>= 1;
      }
   }
   bytes_used  = (status.f_blocks - status.f_bfree) * status.f_bsize;
   bytes_free  = status.f_bfree * status.f_bsize;
   bytes_total = status.f_blocks * status.f_bsize;
   percent     = ((float) bytes_used / bytes_total) * 100.0;

   if (! final_info) {
      summarize_simple(total, counter, hiddenfiles);
      return;
   }

   ioctl(0, TIOCGWINSZ, &w);
   cols = w.ws_col;
   
   printf("\033[0m");
   for (i = 0; i < cols; ++i)
      printf("=");
   
    bytes_used_string  = colorize_bytes(bytes_used, SCHEME_STATUS, -1);
    bytes_free_string  = colorize_bytes(bytes_free, SCHEME_STATUS, -1);
    bytes_total_string = colorize_bytes(total, SCHEME_FILES, 11);
    
   if (hiddenfiles) {
      printf("\n%s in %ld%s+%ld%s files - %s: %s%s kB used (%02.0f%%), %s%s kB free\n", bytes_total_string,
               counter, COLOR_GREY_CODE, hiddenfiles, COLOR_WHITE_CODE, get_filesystem(&status), 
            bytes_used_string, COLOR_WHITE_CODE, percent, bytes_free_string, COLOR_WHITE_CODE);
    } else {
      printf("\n%s in %ld files - %s: %s%s kB used (%02.0f%%), %s%s kB free\n", bytes_total_string, counter,
            get_filesystem(&status), bytes_used_string, COLOR_WHITE_CODE, percent, bytes_free_string,
            COLOR_WHITE_CODE);
    }
   
    free(bytes_used_string);
    free(bytes_free_string);
    free(bytes_total_string);
}

int
main(int argc, char **argv)
{
   int got_statfs, c, index, num_dirs;
   long long total;
   long counter, hiddenfiles;
   struct statfs status;
    
    while ((c=getopt_long(argc, argv, shortopts, long_options, &index)) != -1) {
      switch (c) {
         case 0:
            break;
         case 'a':
            opt_all = 1;
            break;
         case 'd':
            opt_dir = 1;
            break;   
         case 'h':
            opt_hid = 1;
            break; 
         case 'L':
            opt_nolink = 1;
            break;
         case 's':
            opt_size = 1;
            break;
         case 't':
            opt_time = 1;
            break;
         case '?':
            break;
         default:
            printf("invalid option %d\n", c);
            usage(argv[0]);
            exit(1);
      }
    }

   if (opt_help) {
      usage(argv[0]);
      exit(0);
   }
    
   /* read $LS_COLORS from the environment */
   set_dircolors();
   
   total = counter = hiddenfiles = got_statfs = 0;
   num_dirs = argc - optind;
   
   /* prints out a blank line */
   fprintf(stdout, "\n");

   if (optind == argc) {
      char curr_dir[PATH_MAX];
      
      got_statfs = 1;
      if (getcwd(curr_dir, sizeof(curr_dir)) != curr_dir) {
         perror("getcwd");
         exit(1);
      }
      list_entries(curr_dir, &total, &counter, &hiddenfiles, false);

      if ((statfs(curr_dir, &status)) < 0) {
         fprintf(stderr, "statfs %s: %s\n", curr_dir, strerror(errno));
         exit(1);
      }

   } else {
      struct stat entry_status;
      long long total_local;
      long counter_local, hidden_local;
      
      if (optind < argc) {
         if (statfs(argv[optind], &status) == 0)
            got_statfs = 1;
      }

       while (optind < argc) {
         if ((stat(argv[optind], &entry_status)) < 0) {
            if ((lstat(argv[optind], &entry_status)) == 0)
               list_entries(argv[optind], &total_local, &counter_local, &hidden_local, true);
            else
               fprintf(stderr, "lstat %s: %s\n", argv[optind], strerror(errno));
            optind++;
            continue;
         } else if (! got_statfs){
            /* take statfs having this file as a reference */
            if (statfs(argv[optind], &status) == 0)
               got_statfs = 1;
         }
         
         if (S_ISDIR(entry_status.st_mode) && num_dirs > 1)
            printf("%s%s%s\n", COLOR_YELLOW_CODE, argv[optind], COLOR_WHITE_CODE);
            
         total_local = 0, counter_local = 0, hidden_local = 0;
         list_entries(argv[optind], &total_local, &counter_local, &hidden_local, false);
         
         if (S_ISDIR(entry_status.st_mode) && num_dirs > 1) {
            summarize(status, total_local, counter_local, hidden_local, 0);
            if (optind < argc - 1)
               printf("\n");
         }

         total += total_local;
         hiddenfiles += hidden_local;
         counter += counter_local;
         optind++;
      }

   }

   if (got_statfs)
      summarize(status, total, counter, hiddenfiles, 1);
   free(colors);
   exit(EXIT_SUCCESS);
}

