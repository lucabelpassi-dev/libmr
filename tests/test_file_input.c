file_input_t *fi;
mr_file_line_t line;
file_input_open(&fi, "tests/data/sample");  // directory con 2 file

while (file_input_next(fi, &line) == 1) {
    printf("[%s:%lu] (%zu byte): %.*s\n", 
           line.file_name, line.line_number,
           line.line_len, (int)line.line_len, line.line);
}
file_input_close(fi);

/* Usiamo il %.*s con line_len esplicito: non usiamo mai printf("%s", line.line) perché line.line non è garantita
essere \0-terminata.
Anche se nel nostro caso lo è (getline ce lo dà NUL-terminated), il chiamante non deve assumerlo. */
