// server_operations.c
#include "operations.h"


bool_t
log_operation_1_svc(operation_data arg1, void *result,  struct svc_req *rqstp)
{
    bool_t retval;

    /*
     * insert server code here
     */

    // Imprime la operación en el servidor
    printf("%s\t%s\t%s\n", arg1->username, arg1->operation, arg1->timestamp);

    result = 0;

    return retval;
}

int
operations_prog_1_freeresult (SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{
    xdr_free (xdr_result, result);

    /*
     * Insert additional freeing code here, if needed
     */

    return 1;
}
