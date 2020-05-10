# Simple File Transfer

Create your own branch and do the following exercise in your own directory. Use TCP socket to do the following exercise.

### Data structure
```c
#ifndef DATA_STRUCTURE_H
#define DATA_STRUCTURE_H

#include <stdint.h>
#define BUFFER_SIZE 1500
#define READ_FILE_BUF 2000
typedef struct __attribute__((__packed__)) {
  uint8_t filename_len; // the length of filename.
  char filename[255];   // filename.
  uint64_t file_size;   // the size of file to be sent.
  char content[1];      // the file content, must be "struct hack".
} packet;

#endif
```

### Server Flows
1. Bind TCP socket to some host and port.
2. Must be able to accept connections from the client.
3. Once it accepts a connection from the client, do read from client file descriptor with size `BUFFER_SIZE`. Make sure you use the data structure explained above to store the read buffer.
4. In the first read, you must be able to determine the filename and how big the file is based on the struct mentioned above. If the data is not long enough to determine filename and content length, you are responsible to do read again until you get it (By this time the content of the file may also be retrieved partially).
5. Create the file (open file) in the `uploaded_files` directory with the name `char *filename;` (from the struct).
6. If you have the content in the first read, write it to the opened file buffer.
7. If the content is not complete, you have to read again from the client file descriptor, every read cycle you must write to the opened file buffer and reuse the buffer to read again from the client file descriptor, and so on until the file is transferred from client to server completely.
8. Don't forget to close the file descriptor of the file and client.

### Client Flows
1. Must be able to connect to the server.
2. Ask for a filename from `stdin`.
3. Check the given filename whether it exists or not. If it exists, go on.
4. Use the `stat` syscall to determine the file size.
5. Store the filename, filename length, and file size to the corresponding struct member.
6. Open the file to read with the size `READ_FILE_BUF` and store the buffer to `char content[1];` (from the struct).
7. Send the current struct to the server by writing to opened socket file descriptor.
8. If the file size is greater than `READ_FILE_BUF`, you have to read it again with past buffer (reuse) and send it to the server until the file is completely sent.

### Example Scenes of Work

#### Run the server.
```
$ ./server 127.0.0.1 8000
Listening on 127.0.0.1:8000...
```

#### Generate file and send to it the server.
```
# Generate random file 1 GB.
$ head -c 1073741824 /dev/urandom > test_file

# Checksum file.
$ md5sum test_file
d4ad559cdc24513045a04c0d638555c1  test_file

# Send the file to server.
$ ./client 127.0.0.1 8000
Connecting to 127.0.0.1 8000...
Connection established!
Enter the filename to be sent: test_file
Sending file...
```

#### Server receiving the file.
```
$ ./server 127.0.0.1 8000
Listening on 127.0.0.1:8000
Accepting connection from (<client_ip>:<client_port>)...
Waiting for file...

=== File Info ===
Filename: "test_file"
File size: 1073741824 bytes
=================
The file will be stored at "/home/ammarfaizi2/project/now/simple_file_transfer/ammarfaizi2/uploaded_files/test_file".
Receiving file...
```

#### Client finished.
```
$ ./client 127.0.0.1 8000
Connecting to 127.0.0.1 8000...
Connection established!
Enter the filename to be sent: test_file
Sending file...
Finished!
```

#### Server finished and check the file checksum.
```
$ ./server 127.0.0.1 8000
Listening on 127.0.0.1:8000
Accepting connection from (<client_ip>:<client_port>)...
Waiting for file...

=== File Info ===
Filename: "test_file"
File size: 1073741824 bytes
=================
The file will be stored at "/home/ammarfaizi2/project/now/simple_file_transfer/ammarfaizi2/uploaded_files/test_file".
Receiving file...
File received completely!

# To make sure file is not corrupted, the checksum hash must be the same with the client's checksum hash.
$ md5sum /home/ammarfaizi2/project/now/simple_file_transfer/ammarfaizi2/uploaded_files/test_file
d4ad559cdc24513045a04c0d638555c1 /home/ammarfaizi2/project/now/simple_file_transfer/ammarfaizi2/uploaded_files/test_file
```
