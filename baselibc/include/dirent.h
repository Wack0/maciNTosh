struct dirent {
  unsigned char _unknown_00[0xe];
  unsigned char d_type;
  unsigned char _unknown_0f;
  char d_name[];
};
#define DT_DIR 0x10

typedef void DIR;

DIR *opendir(const char *name);
struct dirent *readdir(DIR *);
int closedir(DIR *);
