#include "channel.h"
#include "buffer.h"
#include <stdlib.h>

// Creates a new channel with the provided size and returns it to the caller
channel_t* channel_create(size_t size)
{
    /* IMPLEMENT THIS */
    channel_t* ch = malloc(sizeof(channel_t));   // allocate channel struct
    if (!ch) return NULL;
    ch->buffer = buffer_create(size);            // create underlying buffer
    if (!ch->buffer) { free(ch); return NULL; }
    if (pthread_mutex_init(&ch->lock, NULL) != 0) { // init mutex
        buffer_free(ch->buffer);
        free(ch);
        return NULL;
    }
    pthread_cond_init(&ch->not_full, NULL);     // signal when buffer has space
    pthread_cond_init(&ch->not_empty, NULL);    // signal when buffer has data
    ch->closed = false;                         // channel starts open
    return ch;
}

// Helper: block until there is room to send or channel is closed
static enum channel_status _wait_and_check_send(channel_t* ch) {
    while (buffer_current_size(ch->buffer) == buffer_capacity(ch->buffer) && !ch->closed) {
        pthread_cond_wait(&ch->not_full, &ch->lock);
    }
    if (ch->closed) return CLOSED_ERROR;
    return SUCCESS;
}

// Helper: block until there is data to receive or channel is closed
static enum channel_status _wait_and_check_recv(channel_t* ch) {
    while (buffer_current_size(ch->buffer) == 0 && !ch->closed) {
        pthread_cond_wait(&ch->not_empty, &ch->lock);
    }
    if (buffer_current_size(ch->buffer) == 0 && ch->closed) return CLOSED_ERROR;
    return SUCCESS;
}

// Writes data to the given channel
// This is a blocking call i.e., the function only returns on a successful completion of send
// In case the channel is full, the function waits till the channel has space to write the new data
// Returns SUCCESS for successfully writing data to the channel,
// CLOSED_ERROR if the channel is closed, and
// GENERIC_ERROR on encountering any other generic error of any sort
enum channel_status channel_send(channel_t *channel, void* data)
{
    /* IMPLEMENT THIS */
    if (!channel) return GENERIC_ERROR;
    pthread_mutex_lock(&channel->lock);
    enum channel_status st = _wait_and_check_send(channel);
    if (st != SUCCESS) {
        pthread_mutex_unlock(&channel->lock);
        return st;          // channel closed
    }
    if (buffer_add(channel->buffer, data) != BUFFER_SUCCESS) {
        pthread_mutex_unlock(&channel->lock);
        return GENERIC_ERROR;
    }
    pthread_cond_signal(&channel->not_empty); // notify receivers
    pthread_mutex_unlock(&channel->lock);
    return SUCCESS;
}

// Reads data from the given channel and stores it in the function's input parameter, data (Note that it is a double pointer)
// This is a blocking call i.e., the function only returns on a successful completion of receive
// In case the channel is empty, the function waits till the channel has some data to read
// Returns SUCCESS for successful retrieval of data,
// CLOSED_ERROR if the channel is closed, and
// GENERIC_ERROR on encountering any other generic error of any sort
enum channel_status channel_receive(channel_t* channel, void** data)
{
    /* IMPLEMENT THIS */
    if (!channel || !data) return GENERIC_ERROR;
    pthread_mutex_lock(&channel->lock);
    enum channel_status st = _wait_and_check_recv(channel);
    if (st != SUCCESS) {
        pthread_mutex_unlock(&channel->lock);
        return st;          // channel closed or error
    }
    if (buffer_remove(channel->buffer, data) != BUFFER_SUCCESS) {
        pthread_mutex_unlock(&channel->lock);
        return GENERIC_ERROR;
    }
    pthread_cond_signal(&channel->not_full); //-- notify senders
    pthread_mutex_unlock(&channel->lock);
    return SUCCESS;
}

// Writes data to the given channel
// This is a non-blocking call i.e., the function simply returns if the channel is full
// Returns SUCCESS for successfully writing data to the channel,
// CHANNEL_FULL if the channel is full and the data was not added to the buffer,
// CLOSED_ERROR if the channel is closed, and
// GENERIC_ERROR on encountering any other generic error of any sort
enum channel_status channel_non_blocking_send(channel_t* channel, void* data)
{
    /* IMPLEMENT THIS */
    if (!channel) return GENERIC_ERROR;
    pthread_mutex_lock(&channel->lock);
    if (channel->closed) { pthread_mutex_unlock(&channel->lock); return CLOSED_ERROR; }
    if (buffer_current_size(channel->buffer) == buffer_capacity(channel->buffer)) {
        pthread_mutex_unlock(&channel->lock);
        return CHANNEL_FULL; //channel is full
    }
    if (buffer_add(channel->buffer, data) != BUFFER_SUCCESS) {
        pthread_mutex_unlock(&channel->lock);
        return GENERIC_ERROR;
    }
    pthread_cond_signal(&channel->not_empty);
    pthread_mutex_unlock(&channel->lock);
    return SUCCESS;
}

// Reads data from the given channel and stores it in the function's input parameter data (Note that it is a double pointer)
// This is a non-blocking call i.e., the function simply returns if the channel is empty
// Returns SUCCESS for successful retrieval of data,
// CHANNEL_EMPTY if the channel is empty and nothing was stored in data,
// CLOSED_ERROR if the channel is closed, and
// GENERIC_ERROR on encountering any other generic error of any sort
enum channel_status channel_non_blocking_receive(channel_t* channel, void** data)
{
    /* IMPLEMENT THIS */
    if (!channel || !data) return GENERIC_ERROR;
    pthread_mutex_lock(&channel->lock);
    if (buffer_current_size(channel->buffer) == 0) {
        pthread_mutex_unlock(&channel->lock);
        return channel->closed ? CLOSED_ERROR : CHANNEL_EMPTY;
    }
    if (buffer_remove(channel->buffer, data) != BUFFER_SUCCESS) {
        pthread_mutex_unlock(&channel->lock);
        return GENERIC_ERROR;
    }
    pthread_cond_signal(&channel->not_full);
    pthread_mutex_unlock(&channel->lock);
    return SUCCESS;
}

// Closes the channel and informs all the blocking send/receive/select calls to return with CLOSED_ERROR
// Once the channel is closed, send/receive/select operations will cease to function and just return CLOSED_ERROR
// Returns SUCCESS if close is successful,
// CLOSED_ERROR if the channel is already closed, and
// GENERIC_ERROR in any other error case
enum channel_status channel_close(channel_t* channel)
{
    /* IMPLEMENT THIS */
    if (!channel) return GENERIC_ERROR;
    pthread_mutex_lock(&channel->lock);
    if (channel->closed) { pthread_mutex_unlock(&channel->lock); return CLOSED_ERROR; //already closed
     }
    channel->closed = true;
    pthread_cond_broadcast(&channel->not_empty);
    pthread_cond_broadcast(&channel->not_full);
    pthread_mutex_unlock(&channel->lock);
    return SUCCESS;
}

// Frees all the memory allocated to the channel
// The caller is responsible for calling channel_close and waiting for all threads to finish their tasks before calling channel_destroy
// Returns SUCCESS if destroy is successful,
// DESTROY_ERROR if channel_destroy is called on an open channel, and
// GENERIC_ERROR in any other error case
enum channel_status channel_destroy(channel_t* channel)
{
    /* IMPLEMENT THIS */
    if (!channel) return GENERIC_ERROR;
    if (!channel->closed) return DESTROY_ERROR;
    pthread_mutex_destroy(&channel->lock);
    pthread_cond_destroy(&channel->not_empty);
    pthread_cond_destroy(&channel->not_full);
    buffer_free(channel->buffer);
    free(channel);
    return SUCCESS;
}

// Takes an array of channels (channel_list) of type select_t and the array length (channel_count) as inputs
// This API iterates over the provided list and finds the set of possible channels which can be used to invoke the required operation (send or receive) specified in select_t
// If multiple options are available, it selects the first option and performs its corresponding action
// If no channel is available, the call is blocked and waits till it finds a channel which supports its required operation
// Once an operation has been successfully performed, select should set selected_index to the index of the channel that performed the operation and then return SUCCESS
// In the event that a channel is closed or encounters any error, the error should be propagated and returned through select
// Additionally, selected_index is set to the index of the channel that generated the error
enum channel_status channel_select(select_t* channel_list, size_t channel_count, size_t* selected_index) {
    if (!channel_list || channel_count == 0 || !selected_index)
        return GENERIC_ERROR;
    size_t idx = 0;
    while (true) {
        // try each channel in turn
        for (size_t i = 0; i < channel_count; i++) {
            channel_t* ch = channel_list[i].channel;
            if (!ch) continue;
            pthread_mutex_lock(&ch->lock);
            bool closed = ch->closed;
            size_t curr = buffer_current_size(ch->buffer);
            size_t cap  = buffer_capacity(ch->buffer);
            pthread_mutex_unlock(&ch->lock);
            if (channel_list[i].dir == SEND) {
                if (closed) { *selected_index = i; return CLOSED_ERROR; }
                if (curr < cap) {
                    enum channel_status st = channel_send(ch, channel_list[i].data);
                    *selected_index = i;
                    return st;      // sent
                }
            } else {
                if (curr > 0) {
                    void* msg = NULL;
                    enum channel_status st = channel_receive(ch, &msg);
                    channel_list[i].data = msg;
                    *selected_index = i;
                    return st;      // received
                }
                if (closed) { *selected_index = i; return CLOSED_ERROR; }
            }
        }
        // none ready: block on current index
        channel_t* ch_wait = channel_list[idx].channel;
        pthread_mutex_lock(&ch_wait->lock);
        if (channel_list[idx].dir == SEND) {
            while (buffer_current_size(ch_wait->buffer) == buffer_capacity(ch_wait->buffer)
                   && !ch_wait->closed) {
                pthread_cond_wait(&ch_wait->not_full, &ch_wait->lock);
            }
        } else {
            while (buffer_current_size(ch_wait->buffer) == 0 && !ch_wait->closed) {
                pthread_cond_wait(&ch_wait->not_empty, &ch_wait->lock);
            }
        }
        pthread_mutex_unlock(&ch_wait->lock);
        idx = (idx + 1) % channel_count;
    }
    return GENERIC_ERROR; // unreachable
}

