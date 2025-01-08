#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <fstream>
#include <algorithm>

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
#define REQUEST_FILE_INFO_TAG 9
#define NUMBER_OF_CHUNKS_TAG 10
#define ACK_NACK_TAG 11
#define BUSYNESS_REQUEST_TAG 12

// The type of messages between peers and tracker
#define REQUEST_PEER_LIST 0
#define DOWNLOAD_REQUEST 1
// Bussiness represents how many chunks a peer has uploaded
#define BUSYNESS_REQUEST 2
#define REQUEST_FILE_INFO 3
#define FINISHED_DOWNLOADING 4
#define SHUTDOWN 5

#define ACK 0
#define NACK 1

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

    int no_of_finished_peers;
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
                MPI_Recv(hash, HASH_SIZE + 1, MPI_CHAR, i, HASH_OF_THE_SEGMENT_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

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
    tracker_args.no_of_finished_peers = 0;

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
                int peer = peers[i];
                MPI_Send(&peer, 1, MPI_INT, status.MPI_SOURCE, SEND_PEER_LIST_TAG, MPI_COMM_WORLD);
            }
        } else if (message == REQUEST_FILE_INFO) {
            
            // The peer wants the information about a file
            char file_name[MAX_FILENAME];
            MPI_Recv(file_name, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, REQUEST_FILE_INFO_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // Send him back the number of segments of the file
            file_struct file = tracker_args.filesMap[file_name];
            int no_of_segments = file.total_no_of_segments;
            MPI_Send(&no_of_segments, 1, MPI_INT, status.MPI_SOURCE, NUMBER_OF_CHUNKS_TAG, MPI_COMM_WORLD);

            // Then, send him the hashes of the segments
            for (int i = 0; i < no_of_segments; i++) {
                const char *hash = {file.hashes[i].c_str()};
                MPI_Send(hash, HASH_SIZE + 1, MPI_CHAR, status.MPI_SOURCE, HASH_OF_THE_SEGMENT_TAG, MPI_COMM_WORLD);
            }

            // If the peer requests the file information, it means that it will
            // slowly download the file, so he should be added to the list
            // of peers that have the file
            int peer = status.MPI_SOURCE;
            tracker_args.peersMap[file_name].push_back(peer);
        } else if (message == FINISHED_DOWNLOADING) {
            tracker_args.no_of_finished_peers++;

            if (tracker_args.no_of_finished_peers == numtasks - 1) {
                // If all peers have finished downloading, the tracker will
                // send a shutdown message to all peers (uploading threads)
                for (int i = 1; i < numtasks; i++) {
                    char message = SHUTDOWN;
                    MPI_Send(&message, 1, MPI_CHAR, i, TRACKER_PEER_TAG, MPI_COMM_WORLD);
                }
                break;
            }
        }
    }
}

void request_file_info(struct peer_struct *peer_args, int i)
{
    struct file_struct file;
    strcpy(file.file_name, peer_args->wanted_files[i].file_name);
    file.current_no_of_segments = 0;

    // Inform the tracker that the peer wants the information about the file
    char message = REQUEST_FILE_INFO;
    MPI_Send(&message, 1, MPI_CHAR, TRACKER_RANK, TRACKER_PEER_TAG, MPI_COMM_WORLD);

    // Send the name of the file to the tracker
    MPI_Send(peer_args->wanted_files[i].file_name, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, REQUEST_FILE_INFO_TAG, MPI_COMM_WORLD);

    // Receive the number of segments of the file
    int no_of_segments;
    MPI_Recv(&no_of_segments, 1, MPI_INT, TRACKER_RANK, NUMBER_OF_CHUNKS_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    file.total_no_of_segments = no_of_segments;

    // Receive the hashes of the segments
    for (int j = 0; j < no_of_segments; j++) {
        char hash[HASH_SIZE + 1];
        MPI_Recv(hash, HASH_SIZE + 1, MPI_CHAR, TRACKER_RANK, HASH_OF_THE_SEGMENT_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        string hash_str(hash);
        file.hashes.push_back(hash_str);
    }
    peer_args->owned_files.push_back(file);
}

vector<int> request_peer_list(struct peer_struct *peer_args, int i)
{
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
        MPI_Recv(&peer, 1, MPI_INT, TRACKER_RANK, SEND_PEER_LIST_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        peers.push_back(peer);
    }

    // Remove myself from the list of peers that have the file
    peers.erase(remove(peers.begin(), peers.end(), peer_args->rank), peers.end());

    // Ask each peer about their total uploaded chunks
    vector<int> uploaded_chunks;
    for (long unsigned int j = 0; j < peers.size(); j++) {
        char message = BUSYNESS_REQUEST;
        MPI_Send(&message, 1, MPI_CHAR, peers[j], TRACKER_PEER_TAG, MPI_COMM_WORLD);

        int no_of_uploads;
        MPI_Recv(&no_of_uploads, 1, MPI_INT, peers[j], BUSYNESS_REQUEST_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        uploaded_chunks.push_back(no_of_uploads);
    }

    // We should order the peers by the number of uploaded chunks
    // in ascending order, that way we can choose the peer with the
    // least uploaded chunks and if he doesn't have the file, we can
    // choose the next peer with the least uploaded chunks
    if (peers.size() >= 2) {
        for (long unsigned int j = 0; j < peers.size() - 1; j++) {
            for (long unsigned int k = j + 1; k < peers.size(); k++) {
                if (uploaded_chunks[j] > uploaded_chunks[k]) {
                    long aux = peers[j];
                    peers[j] = peers[k];
                    peers[k] = aux;

                    aux = uploaded_chunks[j];
                    uploaded_chunks[j] = uploaded_chunks[k];
                    uploaded_chunks[k] = aux;
                }
            }
        }
    }

    return peers;
}

void *download_thread_func(void *arg)
{
    struct peer_struct *peer_args = (struct peer_struct *) arg;

    // For every wanted file, the peer will request the list of peers
    // from the tracker so it can start downloading the file
    for (int i = 0; i < peer_args->no_of_wanted_files; i++) {

        // Request the information about the file (number of segments and hashes)
        request_file_info(peer_args, i);

        // Get the list of peers that have the file we want
        vector<int> peers = request_peer_list(peer_args, i);

        // Choosing the peer from which to download the file
        // We'll choose the peer with the least uploaded chunks
        int best_peer_index = 0;
        int chosen_peer = peers[best_peer_index];

        int current_file = peer_args->owned_files.size() - 1;
        int total_no_of_segments = peer_args->owned_files[current_file].total_no_of_segments;
        int count = 0;

        // Download the file from the chosen peer
        // After 10 iterations, the peer will ask again for the list of peers
        // in order to find the peer with the least uploaded chunks (chosen)
        while (peer_args->owned_files[current_file].current_no_of_segments < total_no_of_segments) {

            // Inform the chosen peer that we want to download the file
            char message = DOWNLOAD_REQUEST;
            MPI_Send(&message, 1, MPI_CHAR, chosen_peer, TRACKER_PEER_TAG, MPI_COMM_WORLD);

            // Send the name of the file
            MPI_Send(peer_args->owned_files[current_file].file_name, MAX_FILENAME, MPI_CHAR, chosen_peer, NAME_OF_THE_FILE_TAG, MPI_COMM_WORLD);

            // Send the number of the segment that we want to download
            MPI_Send(&peer_args->owned_files[current_file].current_no_of_segments, 1, MPI_INT, chosen_peer, HASH_OF_THE_SEGMENT_TAG, MPI_COMM_WORLD);

            // Receive an ACK or NACK from the peer
            char ack;
            MPI_Recv(&ack, 1, MPI_CHAR, chosen_peer, ACK_NACK_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            if (ack == ACK) {
                peer_args->owned_files[current_file].current_no_of_segments++;
                count++;
            } else {
                
                // If the peer doesn't have the segment, we'll choose the next peer
                best_peer_index++;
                if (best_peer_index == (int) peers.size()) {
                    chosen_peer = peers[peers.size() - 1];
                } else {
                    chosen_peer = peers[best_peer_index];
                }
            }

            if (count == 10) {
                peers = request_peer_list(peer_args, i);
                best_peer_index = 0;
                chosen_peer = peers[best_peer_index];

                count = 0;
            }
        }

        // Save the list of hashes in a file
        string output_file_name = "client" + to_string(peer_args->rank) + "_" + peer_args->wanted_files[i].file_name;
        ofstream output_file(output_file_name);
        for (int j = 0; j < peer_args->owned_files[current_file].total_no_of_segments; j++) {
            if (j != peer_args->owned_files[current_file].total_no_of_segments - 1) {
                output_file << peer_args->owned_files[current_file].hashes[j] << endl;
            } else {
                output_file << peer_args->owned_files[current_file].hashes[j];
            }
        }
        output_file.close();

    }

    // Inform the tracker that the peer has finished downloading
    char message = FINISHED_DOWNLOADING;
    MPI_Send(&message, 1, MPI_CHAR, TRACKER_RANK, TRACKER_PEER_TAG, MPI_COMM_WORLD);

    return NULL;
}

void *upload_thread_func(void *arg)
{
    struct peer_struct *peer_args = (struct peer_struct *) arg;

    // Begin receiving messages from peers or tracker
    while (1) {
        MPI_Status status;
        char message;
        MPI_Recv(&message, 1, MPI_CHAR, MPI_ANY_SOURCE, TRACKER_PEER_TAG, MPI_COMM_WORLD, &status);

        if (message == BUSYNESS_REQUEST) {
            int no_of_uploads = peer_args->no_of_uploaded_chunks;
            MPI_Send(&no_of_uploads, 1, MPI_INT, status.MPI_SOURCE, BUSYNESS_REQUEST_TAG, MPI_COMM_WORLD);
        } else if (message == DOWNLOAD_REQUEST) {
            char file_name[MAX_FILENAME];
            MPI_Recv(file_name, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, NAME_OF_THE_FILE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            int segment;
            MPI_Recv(&segment, 1, MPI_INT, status.MPI_SOURCE, HASH_OF_THE_SEGMENT_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // Find the file that the peer has and check if it has the segment
            int file_index = -1;
            for (long unsigned int i = 0; i < peer_args->owned_files.size(); i++) {
                if (strcmp(peer_args->owned_files[i].file_name, file_name) == 0) {
                    file_index = i;
                    break;
                }
            }

            // Check if the peer has the segment
            if (peer_args->owned_files[file_index].current_no_of_segments >= segment) {
                char ack = ACK;
                MPI_Send(&ack, 1, MPI_CHAR, status.MPI_SOURCE, ACK_NACK_TAG, MPI_COMM_WORLD);
                peer_args->no_of_uploaded_chunks++;
            } else {
                char ack = NACK;
                MPI_Send(&ack, 1, MPI_CHAR, status.MPI_SOURCE, ACK_NACK_TAG, MPI_COMM_WORLD);
            }
        } else if (message == SHUTDOWN) {
            break;
        }
    }

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
            MPI_Send(hash, HASH_SIZE + 1, MPI_CHAR, TRACKER_RANK, HASH_OF_THE_SEGMENT_TAG, MPI_COMM_WORLD);
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
