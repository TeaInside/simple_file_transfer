
# Simple File Transfer

Simple File Transfer is a socket programming exercise written in C language.
Everyone can submit pull request after completing the task. Your pull request
will be reviewed by me ([@ammarfaizi2](https://github.com/ammarfaizi2)), you will get suggestion or feedback.

Pull request is welcomed through GitHub repository (https://github.com/TeaInside/simple_file_transfer).


# Task
You have to write TCP client and TCP server. The goal is simply to transfer a
file from client to server.


## Requirements
- Git
- GNU Make
- C compiler (gcc or clang)
- Linux environment


## Installation for Ubuntu
```sh
sudo apt-get install gcc make git -y;
```


## Getting started
- Fork the GitHub repository.
- Clone the forked repository.
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


# Technical Explanation
We have our own protocol.

## Protocol
```c
typedef struct __attribute__((packed)) packet_t {
	uint64_t	file_size;
	uint8_t		file_name_len;
	char		file_name[0xffu];
} packet_t;
```


# License
This project is licensed under the GNU GPL v2 license. There are exceptions for
several directories, please read license_notice.txt
