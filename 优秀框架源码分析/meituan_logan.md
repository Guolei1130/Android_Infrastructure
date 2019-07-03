* 仓库地址：https://github.com/Meituan-Dianping/Logan
* 分析版本：commit id ===> af0355da37abe91e208ab874457fb6b1019314ab

## 日志库

日志库一直是基础架构中很重要且最为基础的一环。自从腾讯 xlog出现之后，各家公司纷纷效仿，以mmap为基础，做跨平台日志。本篇以美团Logan为例，看一下他的设计与实现。

![流程图](https://awps-assets.meituan.net/mit-x/blog-images-bundle-2018a/8cc5b759.png)

Logan与其他日志设计上最大的区别，就是他讲日志写在mmap中，调用flush之后，才会真正的回写到日志文件。

主要由4个部分

* init
* open
* write
* flush

## clogan_init

init方法代码较长，大部分代码在做环境的初始化、文件夹创建等操作，比较重要的是下面部分代码

```c
    int flag = LOGAN_MMAP_FAIL;
    if (NULL == _logan_buffer) {
        if (NULL == _cache_buffer_buffer) {
            flag = open_mmap_file_clogan(cache_path, &_logan_buffer, &_cache_buffer_buffer);
        } else {
            flag = LOGAN_MMAP_MEMORY;
        }
    } else {
        flag = LOGAN_MMAP_MMAP;
    }

    if (flag == LOGAN_MMAP_MMAP) {
        buffer_length = LOGAN_MMAP_LENGTH;
        buffer_type = LOGAN_MMAP_MMAP;
        is_init_ok = 1;
        back = CLOGAN_INIT_SUCCESS_MMAP;
    } else if (flag == LOGAN_MMAP_MEMORY) {
        buffer_length = LOGAN_MEMORY_LENGTH;
        buffer_type = LOGAN_MMAP_MEMORY;
        is_init_ok = 1;
        back = CLOGAN_INIT_SUCCESS_MEMORY;
    } else if (flag == LOGAN_MMAP_FAIL) {
        is_init_ok = 0;
        back = CLOGAN_INIT_FAIL_NOCACHE;
    }

    if (is_init_ok) {
        if (NULL == logan_model) {
            logan_model = malloc(sizeof(cLogan_model));
            if (NULL != logan_model) { //堆非空判断 , 如果为null , 就失败
                memset(logan_model, 0, sizeof(cLogan_model));
            } else {
                is_init_ok = 0;
                printf_clogan("clogan_init > malloc memory fail for logan_model\n");
                back = CLOGAN_INIT_FAIL_NOMALLOC;
                return back;
            }
        }
        if (flag == LOGAN_MMAP_MMAP) //MMAP的缓存模式,从缓存的MMAP中读取数据
            read_mmap_data_clogan(_dir_path);
        printf_clogan("clogan_init > logan init success\n");
    } else {
        printf_clogan("clogan_open > logan init fail\n");
        // 初始化失败，删除所有路径
        if (NULL != _dir_path) {
            free(_dir_path);
            _dir_path = NULL;
        }
        if (NULL != _mmap_file_path) {
            free(_mmap_file_path);
            _mmap_file_path = NULL;
        }
    }
    return back;
```

* open_mmap_file_clogan是初始化buffer的，如果mmap成功，那么是mmap映射的内存buffer，如果不成功，则是申请的内存buffer
* 随后 初始化cLogan_model(这个结构体很重要)，设置buffer类型，长度等等

#### open_mmap_file_clogan

```C
//创建MMAP缓存buffer或者内存buffer
int open_mmap_file_clogan(char *_filepath, unsigned char **buffer, unsigned char **cache) {
    int back = LOGAN_MMAP_FAIL;
    if (NULL == _filepath || 0 == strnlen(_filepath, 128)) {
        back = LOGAN_MMAP_MEMORY;
    } else {
        unsigned char *p_map = NULL;
        int size = LOGAN_MMAP_LENGTH;
        int fd = open(_filepath, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP); //后两个添加权限
        int isNeedCheck = 0; //是否需要检查mmap缓存文件重新检查
        if (fd != -1) { //保护
            int isFileOk = 0;
            FILE *file = fopen(_filepath, "rb+"); //先判断文件是否有值，再mmap内存映射
            if (NULL != file) {
                fseek(file, 0, SEEK_END);
                long longBytes = ftell(file); //文件大小
                if (longBytes < LOGAN_MMAP_LENGTH) {
                    fseek(file, 0, SEEK_SET);
                    char zero_data[size];
                    memset(zero_data, 0, size);
                    size_t _size = 0;
                    _size = fwrite(zero_data, sizeof(char), size, file);
                    fflush(file); //文件全填0
                    if (_size == size) {
                        printf_clogan("copy data 2 mmap file success\n");
                        isFileOk = 1;
                        isNeedCheck = 1;
                    } else {
                        isFileOk = 0;
                    }
                } else {
                    isFileOk = 1;
                }
                fclose(file);
            } else {
                isFileOk = 0;
            }

            if (isNeedCheck) { //加强保护，对映射的文件要有一个适合长度的文件
                FILE *file = fopen(_filepath, "rb");
                if (file != NULL) {
                    fseek(file, 0, SEEK_END);
                    long longBytes = ftell(file);
                    if (longBytes >= LOGAN_MMAP_LENGTH) {
                        isFileOk = 1;
                    } else {
                        isFileOk = 0;
                    }
                    fclose(file);
                } else {
                    isFileOk = 0;
                }
            }

            if (isFileOk) {
                //mmap映射文件
                p_map = (unsigned char *) mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            }
            if (p_map != MAP_FAILED && NULL != p_map && isFileOk) {
                back = LOGAN_MMAP_MMAP;
            } else {
                back = LOGAN_MMAP_MEMORY;
                printf_clogan("open mmap fail , reason : %s \n", strerror(errno));

            }
            close(fd);

            if (back == LOGAN_MMAP_MMAP &&
                access(_filepath, F_OK) != -1) { //在返回mmap前,做最后一道判断，如果有mmap文件才用mmap
                back = LOGAN_MMAP_MMAP;
                *buffer = p_map; //将映射出来的指针赋值给buffer
            } else {
                back = LOGAN_MMAP_MEMORY;
                if (NULL != p_map)
                    munmap(p_map, size);
            }
        } else {
            printf_clogan("open(%s) fail: %s\n", _filepath, strerror(errno));
        }
    }

    int size = LOGAN_MEMORY_LENGTH;
    unsigned char *tempData = malloc(size); // 申请一块长度为size的内存buffer
    if (NULL != tempData) {
        memset(tempData, 0, size); // 这块buffer拿0填充
        *cache = tempData; //这块buffer就是内存cache
        if (back != LOGAN_MMAP_MMAP) { //如果mmap没成功的话,buffer就是使用内存了
            *buffer = tempData;
            back = LOGAN_MMAP_MEMORY; //如果文件打开失败、如果mmap映射失败，走内存缓存
        }
    } else {
        if (back != LOGAN_MMAP_MMAP)
            back = LOGAN_MMAP_FAIL;
    }
    return back;
}
```

上面的代码中，我补充了一部分注释，这个方法的目的就是为了

* mmap成功时，后续数据写入使用mmap映射出来的buffer
* 没成功，申请一块buffer，后续使用

## clogan_open

```C
//省去一些代码
        if (!init_file_clogan(logan_model)) {  //初始化文件IO和文件大小
            is_open_ok = 0;
            back = CLOGAN_OPEN_FAIL_IO;
            return back;
        }

        if (init_zlib_clogan(logan_model) != Z_OK) { //初始化zlib压缩
            is_open_ok = 0;
            back = CLOGAN_OPEN_FAIL_ZLIB;
            return back;
        }

        logan_model->buffer_point = _logan_buffer;

        if (buffer_type == LOGAN_MMAP_MMAP) {  //如果是MMAP,缓存文件目录和文件名称
            cJSON *root = NULL;
            Json_map_logan *map = NULL;
            root = cJSON_CreateObject();
            map = create_json_map_logan();
            char *back_data = NULL;
            if (NULL != root) {
                if (NULL != map) {
                    add_item_number_clogan(map, LOGAN_VERSION_KEY, CLOGAN_VERSION_NUMBER);
                    add_item_string_clogan(map, LOGAN_PATH_KEY, pathname);
                    inflate_json_by_map_clogan(root, map);
                    back_data = cJSON_PrintUnformatted(root);
                }
                cJSON_Delete(root);
                if (NULL != back_data) {
                    add_mmap_header_clogan(back_data, logan_model); //写入mmap header，包含logan version，path name
                    free(back_data);
                } else {
                    logan_model->total_point = _logan_buffer;
                    logan_model->total_len = 0;
                }
            } else {
                logan_model->total_point = _logan_buffer;
                logan_model->total_len = 0;
            }

            logan_model->last_point = logan_model->total_point + LOGAN_MMAP_TOTALLEN;

            if (NULL != map) {
                delete_json_map_clogan(map);
            }
        } else {
            logan_model->total_point = _logan_buffer;
            logan_model->total_len = 0;
            logan_model->last_point = logan_model->total_point + LOGAN_MMAP_TOTALLEN;
        }
        restore_last_position_clogan(logan_model);
        init_encrypt_key_clogan(logan_model);
        logan_model->is_ok = 1;
        is_open_ok = 1;
```

* 初始化文件
* 初始化Zlib
* 写入mmap header

## clogan_write

```c
//省去一些校验代码
    Construct_Data_cLogan *data = construct_json_data_clogan(log, flag, local_time, thread_name,
                                                             thread_id, is_main);
    if (NULL != data) {
        clogan_write_section(data->data, data->data_len);
        construct_data_delete_clogan(data);
        back = CLOGAN_WRITE_SUCCESS;
    } else {
        back = CLOGAN_WRITE_FAIL_MALLOC;
    }
    return back;
```

* 首先，将日志格式化
* 然后调用clogan_write_section写入数据

#### clogan_write_section

```c
void clogan_write_section(char *data, int length) {
    int size = LOGAN_WRITE_SECTION;
    int times = length / size;
    int remain_len = length % size;
    char *temp = data;
    int i = 0;
    for (i = 0; i < times; i++) {
        clogan_write2(temp, size);
        temp += size;
    }
    if (remain_len) {
        clogan_write2(temp, remain_len);
    }
}
```

这是Logan的另一个特点，日志分片，如果单条日志量过大，则进行分片写入。

#### clogan_write2

```c
void clogan_write2(char *data, int length) {
    if (NULL != logan_model && logan_model->is_ok) {
        clogan_zlib_compress(logan_model, data, length); //压缩写入
        update_length_clogan(logan_model); //有数据操作,要更新数据长度到缓存中
        int is_gzip_end = 0;

        if (!logan_model->file_len ||
            logan_model->content_len >= LOGAN_MAX_GZIP_UTIL) { //是否一个压缩单元结束
            clogan_zlib_end_compress(logan_model);
            is_gzip_end = 1;
            update_length_clogan(logan_model);
        }

        int isWrite = 0;
        if (!logan_model->file_len && is_gzip_end) { //如果是个空文件、第一条日志写入
            isWrite = 1;
            printf_clogan("clogan_write2 > write type empty file \n");
        } else if (buffer_type == LOGAN_MMAP_MEMORY && is_gzip_end) { //直接写入文件
            isWrite = 1;
            printf_clogan("clogan_write2 > write type memory \n");
        } else if (buffer_type == LOGAN_MMAP_MMAP &&
                   logan_model->total_len >=
                   buffer_length / LOGAN_WRITEPROTOCOL_DEVIDE_VALUE) { //如果是MMAP 且 文件长度已经超过三分之一
            isWrite = 1;
            printf_clogan("clogan_write2 > write type MMAP \n");
        }
        if (isWrite) { //写入
            write_flush_clogan();
        } else if (is_gzip_end) { //如果是mmap类型,不回写IO,初始化下一步
            logan_model->content_len = 0;
            logan_model->remain_data_len = 0;
            init_zlib_clogan(logan_model);
            restore_last_position_clogan(logan_model);
            init_encrypt_key_clogan(logan_model);
        }
    }
}
```

* clogan_zlib_compress 使用zlib压缩，并将数据写入buffer
* clogan_zlib_end_compress 一个压缩单元结束 结束压缩

#### clogan_zlib_compress

```c
void clogan_zlib_compress(cLogan_model *model, char *data, int data_len) {
    if (model->zlib_type == LOGAN_ZLIB_ING || model->zlib_type == LOGAN_ZLIB_INIT) {
        model->zlib_type = LOGAN_ZLIB_ING;
        clogan_zlib(model, data, data_len, Z_SYNC_FLUSH);
    } else {
        init_zlib_clogan(model);
    }
}
```

重点看一下```clogan_zlib```

```C
        unsigned int have;
        unsigned char out[LOGAN_CHUNK];
        z_stream *strm = model->strm;

        strm->avail_in = (uInt) data_len; // 数据长度
        strm->next_in = (unsigned char *) data; // 数据
        do {
            strm->avail_out = LOGAN_CHUNK; //buffer长度
            strm->next_out = (unsigned char *) out; //存储压缩了的数据
            ret = deflate(strm, type); // 压缩
            if (Z_STREAM_ERROR == ret) {
                deflateEnd(model->strm);

                model->is_ready_gzip = 0;
                model->zlib_type = LOGAN_ZLIB_END;
            } else {
                have = LOGAN_CHUNK - strm->avail_out;  //压缩后的数据长度
                int total_len = model->remain_data_len + have; //上次保留数据长度+压缩后数据长度
                unsigned char *temp = NULL;
                int handler_len = (total_len / 16) * 16; //整数个 16
                int remain_len = total_len % 16; // 余数
                if (handler_len) {
                    int copy_data_len = handler_len - model->remain_data_len;
                    char gzip_data[handler_len];
                    temp = (unsigned char *) gzip_data;
                    if (model->remain_data_len) {
                        memcpy(temp, model->remain_data, model->remain_data_len); //先复制上次保留的到数组里
                        temp += model->remain_data_len; // 指针后移
                    }
                    memcpy(temp, out, copy_data_len); //填充剩余数据和压缩数据
                    aes_encrypt_clogan((unsigned char *) gzip_data, model->last_point, handler_len,
                                       (unsigned char *) model->aes_iv); //把加密数据写入缓存
                    model->total_len += handler_len;
                    model->content_len += handler_len;
                    model->last_point += handler_len;
                }
                if (remain_len) {
                    if (handler_len) {
                        int copy_data_len = handler_len - model->remain_data_len; // 整数倍长度-上次保留数据长度 = 本次该偏移长度
                        temp = (unsigned char *) out;
                        temp += copy_data_len; //指针偏移
                        memcpy(model->remain_data, temp, remain_len); //填充剩余数据和压缩数据
                    } else {
                        temp = (unsigned char *) model->remain_data;
                        temp += model->remain_data_len;
                        memcpy(temp, out, have);
                    }
                }
                model->remain_data_len = remain_len;
            }
        } while (strm->avail_out == 0);
        //省去一些代码
```

上面的代码中，我加入了一些注释，从注释中能够很清晰的了解其中的原理。

* 压缩
* 数据填充
* 加密写入
* 调整remain_data、remain_data_len

每一次的写入，不一定完全写入，有可能会留一部分数据在remain_data中伴随下一条日志一起加密写入。

## clogan_flush

```c
int clogan_flush(void) {
    int back = CLOGAN_FLUSH_FAIL_INIT;
    if (!is_init_ok || NULL == logan_model) {
        return back;
    }
    write_flush_clogan();
    back = CLOGAN_FLUSH_SUCCESS;
    printf_clogan(" clogan_flush > write flush\n");
    return back;
}
```

```C
void write_flush_clogan() {
    if (logan_model->zlib_type == LOGAN_ZLIB_ING) {
        clogan_zlib_end_compress(logan_model); //结束压缩
        update_length_clogan(logan_model);  //
    }
    if (logan_model->total_len > LOGAN_WRITEPROTOCOL_HEAER_LENGTH) { //长度大于协议头才有效
        unsigned char *point = logan_model->total_point;
        point += LOGAN_MMAP_TOTALLEN;
        write_dest_clogan(point, sizeof(char), logan_model->total_len, logan_model);
        printf_clogan("write_flush_clogan > logan total len : %d \n", logan_model->total_len);
        clear_clogan(logan_model);
    }
}
```

```
void write_dest_clogan(void *point, size_t size, size_t length, cLogan_model *loganModel) {
    if (!is_file_exist_clogan(loganModel->file_path)) { //如果文件被删除,再创建一个文件
        if (logan_model->file_stream_type == LOGAN_FILE_OPEN) {
            fclose(logan_model->file);
            logan_model->file_stream_type = LOGAN_FILE_CLOSE;
        }
        if (NULL != _dir_path) {
            if (!is_file_exist_clogan(_dir_path)) {
                makedir_clogan(_dir_path);
            }
            init_file_clogan(logan_model);
            printf_clogan("clogan_write > create log file , restore open file stream \n");
        }
    }
    if (CLOGAN_EMPTY_FILE == loganModel->file_len) { //如果是空文件插入一行CLogan的头文件
        insert_header_file_clogan(loganModel);
    }
    fwrite(point, sizeof(char), logan_model->total_len, logan_model->file);//写入到文件中
    fflush(logan_model->file);
    loganModel->file_len += loganModel->total_len; //修改文件大小
}
```

最终，写入磁盘。
