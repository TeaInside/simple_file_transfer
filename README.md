
# Short Intro
Simple File Transfer is a socket programming exercise written in C language.
You can submit a pull request after completing the task. Your pull request
will be reviewed by me ([@ammarfaizi2](https://github.com/ammarfaizi2)), and
then you will get suggestion, feedback and maybe a request to change something
before your PR be merged to the main branch.

Note: Target platform is only Linux.

Pull request is welcomed through GitHub repository (https://github.com/TeaInside/simple_file_transfer).


# Task
You have to write TCP client and server. The goal is simply to transfer a
file from client to server.


# Getting started
## Requirements
- Git
- GNU Make
- C compiler (gcc or clang)
- Linux environment

## Installation for Ubuntu
```sh
sudo apt-get install gcc make git -y;
```
## Do the exercise
- Fork the GitHub repository.
- Clone your fork repository.
- Create your directory by copying the template to `users/<your_username>`.
For example:
```sh
cp -rf template users/myusername;
cd users/myusername;
```
- Write your code in your own directory.
- To compile the code, simply you can just use `make` command.
```sh
make;
```
- Test your program, make sure it works perfectly.
- Submit a pull request on GitHub repository (merge to the master branch).


# Technical Explanation
In this exercise, we have our own protocol. The data structure in this protocol is represented by this packed struct:
```c
typedef struct __attribute__((packed)) packet_t {
	uint64_t	file_size;
	uint8_t		file_name_len;
	char		file_name[0xffu];
} packet_t;
```

This struct is sent by the client to server. The file content should be placed after the struct.
To make a better abstraction, you can use the following union:
```c
union uni_pkt {
	packet_t	packet;
	char		raw_buf[N]; // Feel free to control the N size as your buffer size
};
```

File content should be placed at offset `raw_buf[sizeof(packet_t)]` at the beginning of buffer.
In the next cycle of receiving file content, you may overwrite it from `&raw_buf[0]`.


# License
This project is licensed under the GNU GPL v2 license. There are exceptions for
several directories, please read license_notice.txt
