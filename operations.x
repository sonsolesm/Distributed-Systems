struct operation_data {
    string username<>;
    string operation<>;
    string timestamp<>;
};

program OPERATIONS_PROG {
    version OPERATIONS_VERS {
        void LOG_OPERATION(operation_data) = 1;
    } = 1;
} = 0x20000099;
