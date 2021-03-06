#include "sic/sic.h"

static sc_rlint_t _int_rules[] = {
  { "\"",       "STRING",           &_sc_string,        1 },
  { "`",        "NO_CASE_STRING",   &_sc_ncstring,      1 },
  { "[",        "OPTIONAL",         &_sc_optional,      1 },
  { "(",        "PRIORITY",         &_sc_priority,      1 },
  { "$",        "WHITESPACES",      &_sc_whitespaces,   1 },
  { "digit",    "DIGIT",            &_sc_digit,         0 },
  { "num",      "NUMBER",           &_sc_num,           0 },
  { "alpha",    "ALPHA",            &_sc_alpha,         0 },
  { "word",     "WORD",             &_sc_word,          0 },
  { "alnum",    "ALPHA_NUMERIC",    &_sc_alnum,         0 },
  { "eol",      "END_OF_LINE",      &_sc_eol,           0 },
  { "*",        "MULTIPLE",         &_sc_multiple,      1 },
  { "+",        "ONE_MULTIPLE",     &_sc_one_multiple,  1 },
  { "~",        "BYTE",             &_sc_byte,          1 },
  { "#",        "BETWEEN",          &_sc_btwn,          1 },
  { NULL, NULL, NULL, 0 }
};

sic_t* sc_init(sic_t* sic)
{
  sic->_err = 0;
  if (sc_hinit(&sic->internal, 1024, &sc_jenkins_hash, SC_KY_STRING) == NULL ||
    sc_hinit(&sic->strings, 1024, &sc_jenkins_hash, SC_KY_STRING) == NULL ||
    sc_hinit(&sic->save, 1024, &sc_jenkins_hash, SC_KY_STRING) == NULL ||
    sc_cinit(&sic->input, NULL, 0) == NULL ||
    sc_binit(&sic->error._last_err, NULL, 0, NULL) == NULL)
    return (NULL);
  return (_sc_set_intrl(sic));
}

int sc_load_file(sic_t* sic, const char* filepath)
{
  char r;
  FILE* file;
  char buff[4096];
  sc_consumer_t csmr;

  sc_hclear(&sic->strings, 1);
  if ((file = fopen(filepath, "r")) == NULL ||
    sc_cinit(&csmr, NULL, 0) == NULL)
    return (0);
  while (fgets(buff, 4096, file)) //TODO Replace w/ getline
  {
    if ((r = _sc_line_to_rule(sic, &csmr, buff)) == 0 && sic->_err)
      break;
    else if (r == 0)
      fprintf(stderr, "%s: %s\n\t@\'%s\'\n", SIC_STR_WARN, SIC_ERR_RULE_MISSING, buff);
  }
  fclose(file);
  sc_cdestroy(&csmr);
  return (sic->_err ? 0 : 1);
}

int sc_parse(sic_t* sic, const char* str, unsigned size)
{
  char* entry;

  _sc_reset(sic);
  if ((entry = sc_hget(&sic->strings, SIC_ENTRY)) == NULL)
  {
    fprintf(stderr, "%s: No entry point @%s\n", SIC_INT_ERR, SIC_ENTRY);
    return (0);
  }
  if (sc_cset(&sic->input, str, size) == NULL)
    return (0);
  if (_sc_eval_expr(sic, entry) && SIC_CSMR_IS_EOI((&sic->input)))
    return (1);
  //TODO Error: end of input
  return (0);
}

sc_list_t* sc_get(sic_t* sic, const char* key)
{
  return ((sc_list_t*)sc_hget(&sic->save, (const void*)key));
}

void sc_error(sic_t* sic, char flag)
{
  unsigned i = 0;

  if (sic->_err)
    return;
  fprintf(stderr, "error:%u: %s (%s)\n\t", (unsigned)sic->error.pos, sic->error.err, sic->error.param);
  fflush(stderr);
  sc_bprint(&sic->input.bytes, stderr, flag);
  fputs("\n\t", stderr);
  for (i = 0; i < sic->error.pos; ++i)
    fputc(' ', stderr);
  fputs("^\n", stderr);
}

void sc_destroy(sic_t* sic)
{
  _sc_save_clear(sic);
  sc_hdestroy(&sic->save, 0);
  sc_hdestroy(&sic->strings, 1);
  sc_hdestroy(&sic->internal, 0);
  sc_bdestroy(&sic->error._last_err);
  sc_cdestroy(&sic->input);
}

void _sc_reset(sic_t* sic)
{
  sic->_err = 0;
  sic->error.pos = 0;
  sic->error.err = NULL;
  sic->error.param = NULL;
  _sc_save_clear(sic);
}

int _sc_setrl(sic_t* sic, sc_consumer_t* csmr, sc_rl_t* rule)
{
  int identifier = 0;

  rule->save = NULL;
  if (!sc_cstart(csmr, "rule"))
    return (_sc_fatal_err(sic));
  if (!sc_cof(csmr, sic->_symbols) && !(identifier = sc_cidentifier(csmr)))
    return (_sc_internal_err(sic, csmr, SIC_ERR_RULE_MISSING, NULL));
  if (!sc_cends(csmr, "rule", &rule->name))
    return (_sc_fatal_err(sic));
  if (identifier && sc_cchar(csmr, ':'))
  {
    if (!sc_cstart(csmr, "save"))
      return (_sc_fatal_err(sic));
    if (!sc_cidentifier(csmr))
    {
      free(rule->save);
      return (_sc_internal_err(sic, csmr, SIC_ERR_SAVE_MISSING, NULL));
    }
    if (!sc_cends(csmr, "save", &rule->save))
      return (_sc_fatal_err(sic));
  }
  return (1);
}

int _sc_eval_rl(sic_t* sic, sc_consumer_t* csmr, sc_rl_t* rule)
{
  int result;
  unsigned i;
  sc_bytes_t* saved;
  sc_rlfunc funcs[] = { &_sc_eval_intrl, &_sc_eval_strrl };
  sc_hashmp_t* rules[] = { &sic->internal, &sic->strings, NULL };

  if (rule->save)
  {
    if (!sc_cstart(&sic->input, "save") || (saved = sc_bcreate(NULL, 0, NULL)) == NULL)
      return (_sc_fatal_err(sic));
  }
  for (i = 0; rules[i]; ++i)
  {
    if (sc_hhas(rules[i], rule->name))
    {
      result = funcs[i](sic, csmr, rule);
      break;
    }
  }
  if (rules[i] == NULL) //Out of bounds
    return (_sc_internal_err(sic, csmr, SIC_ERR_RULE_NOT_FOUND, rule->name));
  if (result && rule->save)
  {
    if (!sc_cendb(&sic->input, "save", saved) || !_sc_save_add(sic, rule->save, saved))
      return (_sc_fatal_err(sic));
  }
  if (!result && rule->save)
  {
    sc_bdestroy(saved);
    free(saved);
    free(rule->save);
  }
  return (result);
}

int _sc_eval_intrl(sic_t* sic, sc_consumer_t* csmr, sc_rl_t* rule)
{
  sc_rlint_t* int_rule = (sc_rlint_t*)sc_hget(&sic->internal, rule->name);

  if (int_rule == NULL)
    return (0);
  return (int_rule->func(sic, csmr, int_rule));
}

int _sc_eval_strrl(sic_t* sic, sc_consumer_t* csmr, sc_rl_t* rule)
{
  char* str;

  (void)csmr;
  if ((str = (char*)sc_hget(&sic->strings, rule->name)) == NULL)
    return (_sc_internal_err(sic, csmr, SIC_ERR_RULE_NOT_FOUND, rule->name));
  return (_sc_eval_expr(sic, str));
}

int _sc_eval_csmr_expr(sic_t* sic, sc_consumer_t* csmr)
{
  char c;
  sc_rl_t rule;
  char result = 1;
  intptr_t save = sic->input._ptr;

  while (SIC_CSMR_READ(csmr, &c) && c != '|' && !sic->_err)
  {
    sc_cmultiples(csmr, &sc_cwhitespace);
    if (!_sc_setrl(sic, csmr, &rule))
      return (0);
    result = _sc_eval_rl(sic, csmr, &rule) && result;
    sc_cmultiples(csmr, &sc_cwhitespace);
    free(rule.name);
  }
  sic->input._ptr = (!result ? save : sic->input._ptr);
  if (!result && c == '|' && !sic->_err)
  {
    ++csmr->_ptr;
    return (_sc_eval_csmr_expr(sic, csmr));
  }
  return (sic->_err ? 0 : result);
}

int _sc_eval_expr(sic_t* sic, const char* expr)
{
  int result;
  sc_consumer_t csmr;

  if (sc_cinit(&csmr, expr, strlen(expr)) == NULL)
    return (_sc_fatal_err(sic));
  result = _sc_eval_csmr_expr(sic, &csmr);
  sc_cdestroy(&csmr);
  return (result);
}

//Add and update the internal rules to SIC
sic_t* _sc_set_intrl(sic_t* sic)
{
  unsigned i;

  sic->_symbols[0] = 0;
  for (i = 0; _int_rules[i].rule; ++i)
  {
    if (!sc_hadd(&sic->internal, (void*)_int_rules[i].rule, &_int_rules[i]))
    {
      sc_destroy(sic);
      return (NULL);
    }
    if (_int_rules[i].symbol)
      strcat(sic->_symbols, _int_rules[i].rule);
  }
  return (sic);
}

int _sc_string(sic_t* sic, sc_consumer_t* csmr, sc_rlint_t* rlint)
{
  char* str;
  int has_read;

  if (!_sc_tkn_cntnt(sic, csmr, rlint, "\"\"", 1, &str))
    return (0);
  has_read = sc_ctxt(&sic->input, str, 0);
  free(str);
  return (has_read);
}

int _sc_ncstring(sic_t* sic, sc_consumer_t* csmr, sc_rlint_t* rlint)
{
  char* str;
  int has_read;

  if (!_sc_tkn_cntnt(sic, csmr, rlint, "``", 1, &str))
    return (0);
  has_read = sc_ctxt(&sic->input, str, 1);
  free(str);
  return (has_read);
}

int _sc_optional(sic_t* sic, sc_consumer_t* csmr, sc_rlint_t* rlint)
{
  _sc_eval_btwn(sic, csmr, rlint, "[]", 0);
  return (SIC_RETVAL(sic, 1));
}

int _sc_priority(sic_t* sic, sc_consumer_t* csmr, sc_rlint_t* rlint)
{
  return (_sc_eval_btwn(sic, csmr, rlint, "()", 0));
}

int _sc_whitespaces(sic_t* sic, sc_consumer_t* csmr, sc_rlint_t* rlint)
{
  (void)rlint;
  (void)csmr;
  while (sc_cwhitespace(&sic->input));
  return (SIC_RETVAL(sic, 1));
}

int _sc_digit(sic_t* sic, sc_consumer_t* csmr, sc_rlint_t* rlint)
{
  (void)csmr;
  (void)rlint;
  return (sc_cdigit(&sic->input));
}

int _sc_num(sic_t* sic, sc_consumer_t* csmr, sc_rlint_t* rlint)
{
  (void)csmr;
  (void)rlint;
  return (sc_cmultiples(&sic->input, &sc_cdigit));
}

int _sc_alpha(sic_t* sic, sc_consumer_t* csmr, sc_rlint_t* rlint)
{
  (void)csmr;
  (void)rlint;
  return (sc_calpha(&sic->input));
}

int _sc_word(sic_t* sic, sc_consumer_t* csmr, sc_rlint_t* rlint)
{
  (void)csmr;
  (void)rlint;
  return (sc_cmultiples(&sic->input, &sc_calpha));
}

int _sc_alnum(sic_t* sic, sc_consumer_t* csmr, sc_rlint_t* rlint)
{
  (void)csmr;
  (void)rlint;
  return (sc_calphanum(&sic->input));
}

int _sc_eol(sic_t* sic, sc_consumer_t* csmr, sc_rlint_t* rlint)
{
  (void)csmr;
  (void)rlint;
  return (sc_csome(&sic->input, "\r\n"));
}

int _sc_multiple(sic_t* sic, sc_consumer_t* csmr, sc_rlint_t* rlint)
{
  return (_sc_rl_multiple(sic, csmr, rlint, 0));
}

int _sc_one_multiple(sic_t* sic, sc_consumer_t* csmr, sc_rlint_t* rlint)
{
  return (_sc_rl_multiple(sic, csmr, rlint, 1));
}

int _sc_byte(sic_t* sic, sc_consumer_t* csmr, sc_rlint_t* rlint)
{
  char* bytes;
  int has_read = 0;

  if (!sc_cstart(csmr, "byte"))
    return (_sc_fatal_err(sic));
  if (!sc_cmultiples(csmr, &sc_cdigit))
    return (_sc_internal_err(sic, csmr, SIC_ERR_RULE_ERRONEOUS, rlint->name));
  if (!sc_cends(csmr, "byte", &bytes))
    return (_sc_fatal_err(sic));
  has_read = sc_cchar(&sic->input, atoi(bytes));
  free(bytes);
  return (has_read);
}

int _sc_btwn(sic_t* sic, sc_consumer_t* csmr, sc_rlint_t* rlint)
{
  char x, y;
  char* str;

  if (!_sc_tkn_func(sic, csmr, rlint, &sc_cdigit, &str))
    return (0);
  x = (char)atoi(str);
  free(str);
  if (!sc_cchar(csmr, '-'))
    return (_sc_internal_err(sic, csmr, SIC_ERR_RULE_ERRONEOUS, rlint->name));
  if (!_sc_tkn_func(sic, csmr, rlint, &sc_cdigit, &str))
    return (0);
  y = (char)atoi(str);
  free(str);
  return (sc_crange(&sic->input, x, y));
}

int _sc_save_add(sic_t* sic, char* key, sc_bytes_t* save)
{
  void* tmp;
  sc_list_t* list;

  if ((tmp = sc_hget(&sic->save, (const void*)key)) == NULL)
  {
    if ((list = sc_lcreate(NULL)) == NULL ||
      !sc_hadd(&sic->save, (const void*)key, list))
      return (0);
  }
  else
  {
    list = (sc_list_t*)tmp;
    free(key);
  }
  return (sc_ladd(list, save) != NULL);
}

void _sc_save_remove(void* bucket, void* param)
{
  unsigned i;
  sc_list_t* list;

  (void)param;
  list = (sc_list_t*)(((sc_bucket_t*)bucket)->val);
  if (list == NULL)
    return;
  for (i = 0; i < list->size; ++i)
  {
    sc_bdestroy(list->content[i]);
    free(list->content[i]);
  }
  sc_ldestroy(list);
}

void _sc_save_clear(sic_t* sic)
{
  sc_hiterate(&sic->save, &_sc_save_remove, NULL);
  sc_hclear(&sic->save, 1);
}

//Call whenever there is an internal error (rule, ...)
int _sc_internal_err(sic_t* sic, sc_consumer_t* csmr, const char* e, const char* p)
{
  intptr_t i;

  fprintf(stderr, (p ? "%s: %s \'%s\'\n\t@" : "%s: %s\n\t@"), SIC_INT_ERR, e, p);
  fflush(stderr);
  sc_bprint(&csmr->bytes, stderr, 0);
  fputs("\n\t", stderr);
  for (i = 0; i < csmr->_ptr; ++i)
    fputc(' ', stderr);
  fputs("^\n", stderr);
  return (_sc_fatal_err(sic));
}

//Stops SIC from processing
int _sc_fatal_err(sic_t* sic)
{
  sic->_err = 1;
  return (0);
}

//Evaluate the next given rule. Loop on the input until the rule stops to fit.
//MUST fit at least 'n' times
int _sc_rl_multiple(sic_t* sic, sc_consumer_t* csmr, sc_rlint_t* rlint, unsigned n)
{
  sc_rl_t rule;
  intptr_t save;
  unsigned i = 0;
  char rule_correct = 1;

  (void)rlint;
  save = csmr->_ptr;
  while (rule_correct)
  {
    csmr->_ptr = save;
    if (!_sc_setrl(sic, csmr, &rule))
      return (0);
    rule_correct = _sc_eval_rl(sic, csmr, &rule);
    free(rule.name);
    ++i;
  }
  return (i >= n ? SIC_RETVAL(sic, 1) : 0);
}

//Use after a rule => Return content between 2 rule tokens
//Ex: After rule 'STRING' -> hello world"
//    Redo consumer pointer using size of rule -> "hello world"
//    Set 'str' as the content between tokens -> hello world
int _sc_tkn_cntnt(sic_t* sic, sc_consumer_t* csmr, sc_rlint_t* rlint, const char* tokens, char identical, char** str)
{
  sc_bytes_t cntnt;
  unsigned len = strlen(rlint->rule);

  csmr->_ptr -= len;
  if (sc_binit(&cntnt, NULL, 0, NULL) == NULL)
    return (_sc_fatal_err(sic));
  if (!sc_cstart(csmr, "token"))
    return (_sc_fatal_err(sic));
  if (!sc_ctkn(csmr, tokens, identical))
    return (_sc_internal_err(sic, csmr, SIC_ERR_RULE_ERRONEOUS, rlint->name));
  if (!sc_cendb(csmr, "token", &cntnt))
    return (_sc_fatal_err(sic));
  sc_berase(&cntnt, 0, 1);
  sc_berase(&cntnt, cntnt.size - 1, 1);
  if (!sc_bappc(&cntnt, 0))
    return (_sc_fatal_err(sic));
  *str = cntnt.array;
  return (1);
}

//Recover token using a consumer function
//Token is stored in parameter str
int _sc_tkn_func(sic_t* sic, sc_consumer_t* csmr, sc_rlint_t* rlint, sc_csmrfunc func, char** str)
{
  if (!sc_cstart(csmr, "tkn"))
    return (_sc_fatal_err(sic));
  if (!sc_cmultiples(csmr, func))
    return (_sc_internal_err(sic, csmr, SIC_ERR_RULE_ERRONEOUS, rlint->name));
  if (!sc_cends(csmr, "tkn", str))
    return (_sc_fatal_err(sic));
  return (1);
}

int _sc_eval_btwn(sic_t* sic, sc_consumer_t* csmr, sc_rlint_t* rlint, const char* tokens, char identical)
{
  char* str;
  char result;

  if (!_sc_tkn_cntnt(sic, csmr, rlint, tokens, identical, &str))
    return (0);
  result = _sc_eval_expr(sic, str);
  free(str);
  return (result);
}

int _sc_add_srule(sic_t* sic, const char* rule, const char* str)
{
  return (sc_hadd(&sic->strings, (void*)rule, (void*)str) != NULL);
}

int _sc_line_to_rule(sic_t* sic, sc_consumer_t* csmr, const char* line)
{
  char* rulename;
  char* rulecntnt;

  if (sc_cset(csmr, line, strlen(line)) == NULL)
    return (_sc_fatal_err(sic));
  sc_cmultiples(csmr, &sc_cwhitespace);
  if (SIC_CSMR_IS_EOI(csmr)) //Empty line, don't care
    return (1);
  if (!sc_cstart(csmr, "name"))
    return (_sc_fatal_err(sic));
  if (!sc_cidentifier(csmr))
    return (0);
  if (!sc_cends(csmr, "name", &rulename))
    return (_sc_fatal_err(sic));
  sc_cmultiples(csmr, &sc_cwhitespace);
  if (!sc_cchar(csmr, '='))
    return (0);
  if (!sc_cstart(csmr, "content") || !sc_ctoeoi(csmr) ||
    !sc_cends(csmr, "content", &rulecntnt))
    return (_sc_fatal_err(sic));
  return (_sc_add_srule(sic, rulename, rulecntnt));
}