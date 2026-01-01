#include <stdio.h>

#include <lcl.h>
#include <lcl-json.h>

int main(void) {
  lcl_interp *interp = lcl_interp_new();
  lcl_return_code rc;
  lcl_value *result = NULL;

  if (!interp) {
    fprintf(stderr, "Failed to create interpreter\n");
    return 1;
  }

  lcl_register_core(interp);
  lcl_register_json(interp);

  rc = lcl_eval_string(interp,
    "puts {Testing lcl-json...}\n"
    "\n"
    "# Test encode simple values\n"
    "puts \"String: [json::encode {hello}]\"\n"
    "puts \"Number: [json::encode 42]\"\n"
    "puts \"Float: [json::encode 3.14]\"\n"
    "\n"
    "# Test encode list\n"
    "puts \"Array: [json::encode [list 1 2 3]]\"\n"
    "\n"
    "# Test encode dict\n"
    "puts \"Object: [json::encode [dict name Alice age 30]]\"\n"
    "\n"
    "# Test encode booleans and null\n"
    "puts \"True: [json::encode [json::true]]\"\n"
    "puts \"False: [json::encode [json::false]]\"\n"
    "puts \"Null: [json::encode [json::null]]\"\n"
    "\n"
    "# Test dict with booleans\n"
    "let obj_with_bool [dict enabled [json::true] disabled [json::false] data [json::null]]\n"
    "puts \"Object with bool/null: [json::encode $obj_with_bool]\"\n"
    "\n"
    "# Test decode array\n"
    "let parsed [json::decode {[1, 2, 3]}]\n"
    "puts \"Decoded array: $parsed\"\n"
    "puts \"First element: [get $parsed 0]\"\n"
    "\n"
    "# Test decode object\n"
    "let parsed_obj [json::decode {{\"name\": \"Bob\", \"age\": 25}}]\n"
    "puts \"Decoded object: $parsed_obj\"\n"
    "puts \"Name: [get $parsed_obj name]\"\n"
    "\n"
    "# Test decode booleans and null\n"
    "let bool_obj [json::decode {{\"flag\": true, \"other\": false, \"empty\": null}}]\n"
    "puts \"Decoded bool object: $bool_obj\"\n"
    "let flag [get $bool_obj flag]\n"
    "puts \"flag is bool: [json::bool? $flag]\"\n"
    "let empty [get $bool_obj empty]\n"
    "puts \"empty is null: [json::null? $empty]\"\n"
    "\n"
    "# Round-trip test with booleans\n"
    "let original [dict active [json::true] deleted [json::false] meta [json::null]]\n"
    "let json_str [json::encode $original]\n"
    "puts \"Encoded: $json_str\"\n"
    "let decoded [json::decode $json_str]\n"
    "let re_encoded [json::encode $decoded]\n"
    "puts \"Re-encoded: $re_encoded\"\n"
    "\n"
    "puts {All tests passed!}\n",
    &result);

  if (rc != LCL_RC_OK) {
    fprintf(stderr, "Test failed: %s (at %s:%d)\n",
            lcl_interp_error_msg(interp),
            lcl_interp_error_file(interp),
            lcl_interp_error_line(interp));
    lcl_interp_free(interp);
    return 1;
  }

  lcl_ref_dec(result);
  lcl_interp_free(interp);

  return 0;
}
