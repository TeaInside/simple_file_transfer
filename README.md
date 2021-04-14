
# Simple File Transfer

Simple File Transfer is a socket programming exercise written in C language.
Everyone can submit pull request after completing the task. Your pull request
will be reviewed by me ([https://github.com/ammarfaizi2](@ammarfaizi2)), you will get suggestion or feedback.

Pull request is welcomed through GitHub repository (https://github.com/TeaInside/simple_file_transfer).


# Task
You have to write TCP client and TCP server. The goal is simply to transfer file from
client to server.

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
