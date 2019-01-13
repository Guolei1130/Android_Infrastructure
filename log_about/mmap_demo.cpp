#include<sys/mman.h>　
#include<sys/types.h>　
#include<fcntl.h>
#include<string.h>
#include<stdio.h>　
#include<unistd.h>　

#define MMAP_SIZE 1024

int main(int argc, char **argv)//map a normal file as shared mem:　
{
    int fd, i;
    char temp;
    fd = open("/Users/guolei/ClionProjects/hello/test.txt", O_CREAT | O_RDWR, 00777);

    char zero_data[MMAP_SIZE];
    memset(zero_data, 0, MMAP_SIZE);
//    填充数据
    write(fd, zero_data, MMAP_SIZE);

    char *p_mmap = (char *) mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    memcpy(p_mmap, "this is hello", strlen("this is hello"));
    p_mmap += strlen("this is hello");
    memcpy(p_mmap, "this is guolei", strlen("this is guolei"));
    printf("initializeover\n");
}
