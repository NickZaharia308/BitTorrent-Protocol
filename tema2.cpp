#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <fstream>

using namespace std;

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100

// The tags for MPI
#define NUMBER_OF_FILES_TAG 0
#define NAME_OF_THE_FILE_TAG 1
#define NUMBER_OF_SEGMENTS_TAG 2
#define HASH_OF_THE_SEGMENT_TAG 3
#define START_DOWNLOADING_TAG 4
#define TRACKER_PEER_TAG 5
#define REQUEST_PEER_LIST_TAG 6
#define SIZE_OF_PEER_LIST_TAG 7
#define SEND_PEER_LIST_TAG 8

// The type of messages between peers and tracker
#define REQUEST_PEER_LIST 0

struct file_struct {
    char file_name[MAX_FILENAME];
    int total_no_of_segments;
    int current_no_of_segments;
    vector<string> hashes;
};

struct peer_struct {
    int rank;
    int no_of_owned_files;
    int no_of_wanted_files;
    vector<file_struct> owned_files;
    vector<file_struct> wanted_files;
    int no_of_uploaded_chunks;
};

struct tracker_struct {
    // A map is necessary because if two peers send the same file,
    // the tracker should not store the file twice
    map<string, file_struct> filesMap;

    map<string, vector<int>> peersMap;
};

void receive_from_peers(struct tracker_struct *tracker_args, int numtasks)
{
    // Get information from all peers
    for (int i = 1; i < numtasks; i++) {

        // First is the number of files that the peer has (owns)
        int no_of_files;
        MPI_Recv(&no_of_files, 1, MPI_INT, i, NUMBER_OF_FILES_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        for (int j = 0; j < no_of_files; j++) {
            file_struct current_file_struct;

            // Receive the name of the file
            char file_name[MAX_FILENAME];
            MPI_Recv(file_name, MAX_FILENAME, MPI_CHAR, i, NAME_OF_THE_FILE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // Add it to the structure
            strcpy(current_file_struct.file_name, file_name);

            // Receive the number of segments
            int no_of_segments;
            MPI_Recv(&no_of_segments, 1, MPI_INT, i, NUMBER_OF_SEGMENTS_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // Add it to the structure
            current_file_struct.total_no_of_segments = no_of_segments;
            current_file_struct.current_no_of_segments = no_of_segments;

            // Receive the hashes of each segment
            for (int k = 0; k < no_of_segments; k++) {
                char hash[HASH_SIZE + 1];
                MPI_Recv(hash, HASH_SIZE + 1, MPI_CHAR, i, HASH_OF_THE_SEGMENT_TAG + k, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                string hash_str(hash);
                current_file_struct.hashes.push_back(hash_str);
            }

            // Add the file to the tracker structure
            tracker_args->filesMap[file_name] = current_file_struct;

            // Also add the peer to the list of peers/seeds that have this file
            tracker_args->peersMap[file_name].push_back(i);
        }
    }
}

void tracker(int numtasks, int rank) {
    struct tracker_struct tracker_args;

    // Receive the information from all peers (the files in the system,
    // their number of segments, their order and who owns them)
    receive_from_peers(&tracker_args, numtasks);

    // Peers will get the approve from the tracker
    // to start downloading/uploading
    const char approve[3] = {'O', 'K', '\0'};
    for (int i = 1; i < numtasks; i++) {
        MPI_Send(approve, 3, MPI_CHAR, i, START_DOWNLOADING_TAG, MPI_COMM_WORLD);
    }

    // Start receiving messages from peers
    while (1) {
        MPI_Status status;

        char message;
        MPI_Recv(&message, 1, MPI_CHAR, MPI_ANY_SOURCE, TRACKER_PEER_TAG, MPI_COMM_WORLD, &status);

        if (message == REQUEST_PEER_LIST) {

            // The peer wants the list of peers/seeds that have the file
            char file_name[MAX_FILENAME];
            MPI_Recv(file_name, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, REQUEST_PEER_LIST_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // First, send the size of the list of peers
            vector<int> peers = tracker_args.peersMap[file_name];
            int size = peers.size();

            MPI_Send(&size, 1, MPI_INT, status.MPI_SOURCE, SIZE_OF_PEER_LIST_TAG, MPI_COMM_WORLD);

            // The peer will receive the list of peers that have the file
            for (long unsigned int i = 0; i < peers.size(); i++) {
                MPI_Send(&peers[i], 1, MPI_INT, status.MPI_SOURCE, SEND_PEER_LIST_TAG + i, MPI_COMM_WORLD);
            }
        }
    }
}

void *download_thread_func(void *arg)
{
    struct peer_struct *peer_args = (struct peer_struct *) arg;

    // For every wanted file, the peer will request the list of peers
    // from the tracker so it can start downloading the file
    for (int i = 0; i < peer_args->no_of_wanted_files; i++) {

        // Inform the tracker that the peer wants the list of peers that have the file
        char message = REQUEST_PEER_LIST;
        MPI_Send(&message, 1, MPI_CHAR, TRACKER_RANK, TRACKER_PEER_TAG, MPI_COMM_WORLD);

        // Send the name of the file to the tracker
        MPI_Send(peer_args->wanted_files[i].file_name, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, REQUEST_PEER_LIST_TAG, MPI_COMM_WORLD);
    
        // Receive the size of the list of peers that have the file we want
        int size;
        MPI_Recv(&size, 1, MPI_INT, TRACKER_RANK, SIZE_OF_PEER_LIST_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        // Receive the list of peers that have the file we want
        vector<int> peers;
        for (int j = 0; j < size; j++) {
            int peer;
            MPI_Recv(&peer, 1, MPI_INT, TRACKER_RANK, SEND_PEER_LIST_TAG + j, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            peers.push_back(peer);
        }

        // cout << "I am peer " << peer_args->rank << " and I want the file " << peer_args->wanted_files[i].file_name << endl;
        // // Print the list of peers that have the file we want
        // printf("Peers that have the file %s: ", peer_args->wanted_files[i].file_name);
        // for (long unsigned int j = 0; j < peers.size(); j++) {
        //     printf("%d ", peers[j]);
        // }
        // printf("\n");
    }

    return NULL;
}

void *upload_thread_func(void *arg)
{
    struct peer_struct *peer_args = (struct peer_struct *) arg;

    return NULL;
}

void read_input_file(struct peer_struct *peer_args)
{
    string input_file_name = "in" + to_string(peer_args->rank) + ".txt";
    ifstream input_file(input_file_name);
    if (!input_file.is_open()) {
        printf("Eroare la deschiderea fisierului de input\n");
        exit(-1);
    }

    // Reading the number of owned files
    input_file >> peer_args->no_of_owned_files;

    // For every owned file, the peer will read the first line
    // (file name and number of segments) and then the hashes of the segments
    for (int i = 0; i < peer_args->no_of_owned_files; i++) {

        // First line of the file, the peer will have all of the file segments
        file_struct file;
        input_file >> file.file_name;
        input_file >> file.total_no_of_segments;
        file.current_no_of_segments = file.total_no_of_segments;

        // Reading the hashes of the segments
        for (int j = 0; j < file.total_no_of_segments; j++) {
            string current_hash;
            input_file >> current_hash;
            file.hashes.push_back(current_hash);
        }
        peer_args->owned_files.push_back(file);
    }

    // Reading the number of wanted files
    input_file >> peer_args->no_of_wanted_files;

    // For every wanted file, the peer will read just the name of the file
    for (int i = 0; i < peer_args->no_of_wanted_files; i++) {
        file_struct file;
        input_file >> file.file_name;
        peer_args->wanted_files.push_back(file);
    }

    // At first, the peer hasn't uploaded any chunks
    peer_args->no_of_uploaded_chunks = 0;

    input_file.close();
}

void send_to_tracker_initial_information(struct peer_struct *peer_args)
{
    // Sending to tracker the number of owned files
    MPI_Send(&peer_args->no_of_owned_files, 1, MPI_INT, TRACKER_RANK, NUMBER_OF_FILES_TAG, MPI_COMM_WORLD);

    for (int i = 0; i < peer_args->no_of_owned_files; i++) {

        // Sending to tracker the name of the file
        MPI_Send(peer_args->owned_files[i].file_name, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, NAME_OF_THE_FILE_TAG, MPI_COMM_WORLD);

        // Sending to tracker the number of segments
        MPI_Send(&peer_args->owned_files[i].total_no_of_segments, 1, MPI_INT, TRACKER_RANK, NUMBER_OF_SEGMENTS_TAG, MPI_COMM_WORLD);

        // Sending to tracker the hashes of the segments
        for (int j = 0; j < peer_args->owned_files[i].total_no_of_segments; j++) {
            const char *hash = {peer_args->owned_files[i].hashes[j].c_str()};
            MPI_Send(hash, HASH_SIZE + 1, MPI_CHAR, TRACKER_RANK, HASH_OF_THE_SEGMENT_TAG + j, MPI_COMM_WORLD);
        }
    }
}

void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    struct peer_struct peer_args;
    peer_args.rank = rank;

    // Reading the input file and populating the peer_args structure
    // with the information from the input file
    read_input_file(&peer_args);

    // Sending to the tracker the initial information about the 
    // files that the peer owns
    send_to_tracker_initial_information(&peer_args);

    // Waiting for the tracker to send the approve to start downloading/uploading
    char approve[3];
    MPI_Recv(approve, 3, MPI_CHAR, TRACKER_RANK, START_DOWNLOADING_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    r = pthread_create(&download_thread, NULL, download_thread_func, (void *) &peer_args);
    if (r) {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *) &peer_args);
    if (r) {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }
}
 
int main (int argc, char *argv[]) {
    int numtasks, rank;
 
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}
