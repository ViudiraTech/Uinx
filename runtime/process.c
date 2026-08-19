// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "runtime.h"
#include <errno.h>
#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif
int32_t uinx_process_spawn(const char*path,char*const argv[],int64_t*out){if(!path||!out)return-EINVAL;
#if defined(_WIN32)
  (void)argv;return-ENOSYS;
#else
  pid_t p=fork();if(p<0)return-errno;if(p==0){execvp(path,argv);_exit(127);}*out=(int64_t)p;return 0;
#endif
}
int32_t uinx_process_wait(int64_t pid,int32_t*out){
#if defined(_WIN32)
  (void)pid;(void)out;return-ENOSYS;
#else
  int st=0;pid_t r;do{r=waitpid((pid_t)pid,&st,0);}while(r<0&&errno==EINTR);if(r<0)return-errno;if(out)*out=st;return 0;
#endif
}
int32_t uinx_pipe_create(int64_t out[2]){if(!out)return-EINVAL;
#if defined(_WIN32)
  return-ENOSYS;
#else
  int fds[2];if(pipe(fds))return-errno;out[0]=fds[0];out[1]=fds[1];return 0;
#endif
}
