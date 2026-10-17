#pragma once

namespace gs::cli {

int run(int argc, char* argv[]);
int cmd_query(int argc, char* argv[]);
int cmd_doctor(int argc, char* argv[]);
int cmd_debug_facts(int argc, char* argv[]);
int cmd_serve(int argc, char* argv[]);
int cmd_index(int argc, char* argv[]);
int cmd_status(int argc, char* argv[]);

} // namespace gs::cli
