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
};

struct tracker_struct {
    // A map is necessary because if two peers send the same file,
    // the tracker should not store the file twice
    map<string, file_struct> filesMap;

    map<string, vector<int>> peersMap;
};

void *download_thread_func(void *arg)
{
    int rank = *(int*) arg;

    return NULL;
}

void *upload_thread_func(void *arg)
{
    int rank = *(int*) arg;

    return NULL;
}

void receive_from_peers(struct tracker_struct *tracker_args, int numtasks)
{
    // Get information from all peers
    for (int i = 1; i < numtasks; i++) {

        // First is the number of files that the peer has (owns)
        int no_of_files;
        MPI_Recv(&no_of_files, 1, MPI_INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        for (int j = 0; j < no_of_files; j++) {
            file_struct current_file_struct;

            // Receive the name of the file
            char file_name[MAX_FILENAME];
            MPI_Recv(file_name, MAX_FILENAME, MPI_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // Add it to the structure
            strcpy(current_file_struct.file_name, file_name);

            // Receive the number of segments
            int no_of_segments;
            MPI_Recv(&no_of_segments, 1, MPI_INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // Add it to the structure
            current_file_struct.total_no_of_segments = no_of_segments;
            current_file_struct.current_no_of_segments = no_of_segments;

            // Receive the hashes of each segment
            for (int k = 0; k < no_of_segments; k++) {
                char hash[HASH_SIZE + 1];
                MPI_Recv(hash, HASH_SIZE + 1, MPI_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

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

    receive_from_peers(&tracker_args, numtasks);



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

    input_file.close();
}

void send_to_tracker_initial_information(struct peer_struct *peer_args)
{
    // Sending to tracker the number of owned files
    MPI_Send(&peer_args->no_of_owned_files, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);

    for (int i = 0; i < peer_args->no_of_owned_files; i++) {

        // Sending to tracker the name of the file
        MPI_Send(peer_args->owned_files[i].file_name, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

        // Sending to tracker the number of segments
        MPI_Send(&peer_args->owned_files[i].total_no_of_segments, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);

        // Sending to tracker the hashes of the segments
        for (int j = 0; j < peer_args->owned_files[i].total_no_of_segments; j++) {
            const char *hash = {peer_args->owned_files[i].hashes[j].c_str()};
            MPI_Send(hash, HASH_SIZE + 1, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
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

    r = pthread_create(&download_thread, NULL, download_thread_func, (void *) &rank);
    if (r) {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *) &rank);
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
