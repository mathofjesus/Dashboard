#ifndef API_H
#define API_H

#include "http_server.h"
#include "model.h"

int api_roteia(const http_request *req, http_response *res);
void api_set_model(model *m);

#endif /* API_H */
