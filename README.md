# BitTorrent Protocol

## Copyright 2025 Zaharia Nicusor-Alexandru

### Project Idea

The project implements BitTorrent Protocol by using distributed computing
written with MPI.
There are 2 big roles: tracker (only one) and clients.
Clients request from the tracker information about a file and then they
start requesting parts of files from each other in an efficient manner that
I will describe in the next paragraphs.
When all clients have the files they want, the program ends.

### Workflow

#### `Initialization`

There is a tracker that simply gives clients information about files that they
want to download.
It waits for each client to send the list of files that they own and for each
file, the tracker will know the **name of the file**, the **number of segments**
and the **hashes of the segments**.
Basically, when starting, the tracker will know about all the files in the
system and who owns them (seeds) and after that it sends a message to each
client to start downloading or uploading.

On the other side, clients, start with an input file, which contains,
the **number of files they own**, the **name of the files that they own**,
the **number of segments** for each file and the **segments** of the file,
where segments are represented as hashes.
The input file also contains the **number of files they want** and the
**name of the files they want**.
After reading the data, each client sends the tracker the information they own.
When each client finished sending data, the downloading and uploading process
starts (moment when the tracker adds the client as a peer for the file).

#### `Downloading and Uploading`

After sending the information mentioned above to the tracker, the clients start
two threads (one for `downloading` and one for `uploading`).

`Downloading` is required for each file that a client wants.
A request for file info is made, using the function
`request_file_info(struct peer_struct *peer_args, int i)`.
The client sends the tracker the name of the file and receives the number of
the segements for the file and the hash for each segment.
Immediately after that, the client request the list of peers for the file and
starts sending messages to each peer about their **total number of uploads**.
This is the metric I used for efficiency, because I consider that in this
situation is the most reliable to spread work among clients.
Other metrics I could find on [forums](https://www.reddit.com/r/torrents/comments/g42ox6/how_does_a_torrent_decide_which_seed_from_the/)
are mostly based on a sum of factors, but they are relevant about the "distance"
between the peers and their network capacity but these don't apply here, as
message passing in MPI is almost instant.
After each peer responds with its "busyness" (total number of uploads), the
**list of peers** is **sorted in ascending order** by this values.
This way, peers that uploaded less are preferred.
The client that wants to download starts with the first peer on the list and
asks it if it has the current segment (it's very possible for the peer to
not have the entire file because the peer may also try to download from
someone else). The downloading is simulated by sending **ACK** or **NACK**.
If the asked peer has the segment, the downloading continues to the next segment,
else the next peer from the sorted list is asked about the segment (the last
peer in the list will have the entire file for sure).
**To maintain efficiency**, after each 10 downloaded segments, the list of peers
is requested and sorted again, because new peers might have joined the
distribution process.
After downloading an entire file, the client creates an output file with the
content of the downloaded file and continues with the next wanted file.
When the client has downloaded all the files they wanted, a message to
the tracker is sent, then the thread for downloading is closed.

`Uploading` continues until the thread receives a message from the tracker to
shutdown. This message is received when all clients finished downloading the
files that they want.
Beside this, two other messages are accepted, one for requesting the "busyness"
and another for requesting a segment.
For the first one, the client simply responds with the **total number of uploads**
and for the second one, the client checks whether or not it has the segment.

Finally, the `Tracker` waits all clients to send a message representing that all
wanted files have been downloaded.
When all clients finish downloading their files, the Tracker sends a message to
each client (upload thread) to shutdown, then the tracker itself closes.

