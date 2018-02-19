#include "pam_email.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <pwd.h>

#ifndef NO_LDAP
#include <ldap.h>
const char* ldap_attrs[] = {"email", 0};

// uri, base, (filter, (user, (pw)))
void extract_ldap(struct pam_email_ret_t *ret, const char *username, const char *param){
    char* parameters[5]={0,0,0,0,0};
    const char *sep, *last;
    char *filter=0, *tmp_filter, *tmp_filter_next, *first_attribute;
    size_t count_replacements=0;
    const size_t len_username = strlen(username);
    unsigned int ldap_version = LDAP_VERSION3;
    BerElement *ber=0;
    LDAP *ld_h=0;
    LDAPMessage *msg=0, *entry=0;
    last=param;
    for(int c=0; c<5; c++){
        if (last)
            sep=strchr(last, ';');
        if (sep==0){
            switch (c){
                default:
                    ret->state = PAM_AUTH_ERR;
                    goto cleanup_ldap_skip_unbind;
                    break;
                case 1:
                    while(!parameters[1])
                        parameters[1] = strdup(last);
                    while(!parameters[2])
                        parameters[2] = strdup("(uid=?)");
                    assert(parameters[3]==0);
                    assert(parameters[4]==0);
                    break;
                case 2:
                    while(!parameters[2])
                        parameters[2] = strdup(last);
                    break;
                    assert(parameters[3]==0);
                    assert(parameters[4]==0);
                case 3:
                    while(!parameters[3])
                        parameters[3] = strdup(last);
                    break;
                    assert(parameters[4]==0);
                case 4:
                    while(!parameters[4])
                        parameters[4] = strdup(last);
                    break;
            }
            break;
        } else {
            if (sep!=last+1){
                while(!parameters[c])
                    parameters[c] = strndup(last, sep-last);
            }
            last=sep+1;
        }
    }
    tmp_filter = strchr(parameters[2], '?');
    // count replacements
    while(tmp_filter)
    {
        count_replacements++;
        tmp_filter = strchr(tmp_filter+1, '?');
    }
    // create filter, shall never oom
    while (!filter){
        filter = (char*)calloc(len_username*count_replacements+strlen(parameters[2])+1, sizeof(char));
    }
    tmp_filter = parameters[2];
    tmp_filter_next = strchr(tmp_filter, '?');
    if(!tmp_filter_next){
        ret->state = PAM_AUTH_ERR;
        goto cleanup_ldap_skip_unbind;
    }
    while(tmp_filter_next)
    {
        strncat(filter, tmp_filter, tmp_filter_next-tmp_filter);
        strncat(filter, username, strlen(username));
        tmp_filter = tmp_filter_next+1;
        tmp_filter_next = strchr(tmp_filter, '?');
    }
    strncat(filter, tmp_filter, strlen(tmp_filter));
#ifdef PAM_EMAIL_DEBUG_LDAP
    printf("Filter: \"%s\"\n", filter);
#endif
    if (ldap_initialize(&ld_h, parameters[0]) != LDAP_SUCCESS){
        ret->state = PAM_AUTH_ERR;
        goto cleanup_ldap_skip_unbind;
    }

    if (ldap_set_option(ld_h, LDAP_OPT_PROTOCOL_VERSION, &ldap_version) != LDAP_OPT_SUCCESS){
        ret->state = PAM_AUTH_ERR;
        goto cleanup_ldap_skip_unbind;
    }
    printf("success init\n");
    /**
    if(parameter[3]){
        // TODO: prefix with u: ?
    }
    if(parameter[4]){

    }
    if (ldap_sasl_bind_s(ld_h, parameter[3], "", parameter[4], NULL, NULL) != LDAP_SUCCESS ) {
        ret->error = PAM_AUTH_ERR;
        goto cleanup_ldap;
    }*/
    if (ldap_sasl_bind_s(ld_h, NULL, "", NULL, NULL, NULL, NULL) != LDAP_SUCCESS ) {
        ret->state = PAM_AUTH_ERR;
        goto cleanup_ldap;
    }
    printf("success binding\n");

    if (ldap_search_ext_s(ld_h, parameters[1], LDAP_SCOPE_ONELEVEL, filter, (char**)ldap_attrs, 0, NULL, NULL, NULL, 1, &msg)!= LDAP_SUCCESS) {
        goto cleanup_ldap;
    }

    printf("success search\n");
    entry = ldap_first_entry(ld_h, msg);
    printf("entry\n");
    first_attribute = ldap_first_attribute(ld_h, msg, &ber);
    printf("message\n");
    while (!ret->email)
        ret->email=strdup(first_attribute);
    ldap_msgfree(msg);

cleanup_ldap:
    ldap_unbind_ext_s(ld_h, NULL, NULL);
cleanup_ldap_skip_unbind:
    if(ld_h)
        ldap_destroy(ld_h);
    if(filter)
        free(filter);
    for (int c=0; c<5;c++){
        if (parameters[c]){
            free(parameters[c]);
        }
    }
}
#endif

void extract_gecos(struct pam_email_ret_t *ret, const char *username, const char *param){
    char *gecos_full=0, *emailfield=0;
    size_t email_length=0;
    struct passwd *pws = getpwnam (username);
    if (pws){
        while(!gecos_full)
            gecos_full = strdup(pws->pw_gecos);
        endpwent();
    }
    else {
        return;
    }

    // find email field
    emailfield = gecos_full;
    for(int c=0; c<3; c++){
        emailfield=strchr(emailfield, ',');
        if(!emailfield){
            free(gecos_full);
            return;
        }
        // next char after ,
        emailfield+=1;
    }
    // check if it is an email
    if(strchr(emailfield, '@')){
        while(isspace(emailfield[0]) && emailfield[0]!='\0')
            emailfield++;
        while(!isspace(emailfield[email_length]) && emailfield[email_length]!='\0')
            email_length++;
        while(!ret->email)
            ret->email = strndup(emailfield, email_length);
    }
    free(gecos_full);
}

void extract_git(struct pam_email_ret_t *ret, const char *username, const char *param){
    char *fname=0, *home_name=0;
    char *line=NULL;
    size_t line_length=0;
    char* email_begin=0;
    size_t email_length=0, home_length=0;
    struct passwd *pws = getpwnam (username);
    if (pws){
        home_length = strlen(pws->pw_dir);
        while(!home_name)
            home_name = strdup(pws->pw_dir);
        endpwent();
    }
    else {
        return;
    }
    // should not fail because of oom, 12 because of / and \0
    while (!fname){
        fname = calloc(home_length+12, sizeof(char));
    }
    strncpy(fname, home_name, home_length+1);
    // not needed anymore
    free(home_name);
    strncat(fname, "/.gitconfig", 11);
    FILE *gitfile = fopen(fname, "r");
    // not needed anymore
    free(fname);
    if (!gitfile)
        return;
    while(!feof(gitfile)){
        getline(&line, &line_length, gitfile);
        if(strstr(line, "email")){
            email_begin = strchr(line, '=')+1;
            if (!email_begin)
                continue;
            while(isspace(email_begin[0]) && email_begin[0]!='\0')
                email_begin++;
            if (email_begin[0]=='\0')
                continue;
            while(!isspace(email_begin[email_length]) && email_begin[email_length]!='\0')
                email_length++;
            while(!ret->email)
                ret->email = strndup(email_begin, email_length);
            free(line);
            break;
        }
        free(line);
        line = NULL;
    }
    fclose(gitfile);
}


void extract_default(struct pam_email_ret_t *ret, const char *username, const char *param){
    size_t len_username = strlen(username);
    if (param){
        // handle out of memory errors.Try multiple times until giving up
        for (size_t errcount=0; !ret->email; errcount++){
            ret->email = (char *)calloc(len_username+strlen(param)+2, sizeof(char));
#ifdef PAM_EMAIL_ALLOC_ERROR_MAX
            if (errcount>PAM_EMAIL_ALLOC_ERROR_MAX){
                ret->state=PAM_BUF_ERR;
                return;
            }
#endif
        }
        strncpy(ret->email, username, len_username+1);
        ret->email[len_username]='@';
        strncat(ret->email, param, strlen(param));
    } else {
        char hostname[256];
        if(!gethostname(hostname, 255))
            return;
        hostname[255] = '\0';
        // handle out of memory errors.Try multiple times until giving up
        for (size_t errcount=0; !ret->email; errcount++){
            ret->email = (char *)calloc(len_username+strlen(hostname)+2, sizeof(char));
#ifdef PAM_EMAIL_ALLOC_ERROR_MAX
            if (errcount>PAM_EMAIL_ALLOC_ERROR_MAX){
                ret->state=PAM_BUF_ERR;
                return;
            }
#endif
        }
        strncpy(ret->email, username, len_username+1);
        ret->email[len_username]='@';
        strncat(ret->email, hostname, strlen(hostname));
    }
}



// module name is NOT included in argv
struct pam_email_ret_t extract_email(pam_handle_t *pamh, int argc, const char **argv){
    char use_all = 0;
    struct pam_email_ret_t email_ret;
    const char *param=0;
    char *extractor=0;
    const char *username;

    email_ret.email = 0;
    email_ret.state = PAM_SUCCESS;
    if (pam_get_item(pamh, PAM_USER, (const void**)&username)!=PAM_SUCCESS){
        email_ret.state = PAM_AUTH_ERR;
        goto error_extract_email;
    }

    if(argc==0){
        argv = default_argv;
        argc = default_argc;
    }

    for (int countarg=0; countarg < argc; countarg++){
        param = strchr(argv[countarg], '=');
        if (param){
            if (param-argv[countarg] == 0){
                email_ret.state = PAM_AUTH_ERR;
                goto error_extract_email;
            }
            // handle out of memory gracefully, elsewise login or whatever fails
            for (size_t errcount=0; !extractor; errcount++){
                // length +1 for \0
                extractor = (char*)calloc(param-argv[countarg]+1, sizeof(char));
#ifdef PAM_EMAIL_ALLOC_ERROR_MAX
                if (errcount>PAM_EMAIL_ALLOC_ERROR_MAX){
                    email_ret.state=PAM_BUF_ERR;
                    goto error_extract_email;
                }
#endif
            }
            // copy without =, \0 is set by calloc
            strncpy(extractor, argv[countarg], param-argv[countarg]);
            // remove =
            param = param+1;
            // if strlea
        } else {
            // extractor is not freed in this case, so remove const
            extractor = (char *)argv[countarg];
        }

#ifndef NO_LDAP
        // without config not usable => no use_all auto activation
        if (strcmp(extractor, "ldap")==0 && (email_ret.email == 0 && email_ret.state == PAM_SUCCESS)){
            if (param)
                extract_ldap(&email_ret, username, param);
            else {
                fprintf(stderr, "LDAP needs configuration");
            }
        }
#else
        if (strcmp(extractor, "ldap")==0){
            fprintf(stderr, "LDAP is not available");
        }
#endif
        if (strcmp(extractor, "gecos")==0 && (email_ret.email == 0 && email_ret.state == PAM_SUCCESS)){
            extract_gecos(&email_ret, username, param);
        }
        if (strcmp(extractor, "git")==0 && (email_ret.email == 0 && email_ret.state == PAM_SUCCESS)){
            extract_git(&email_ret, username, param);
        }


        // last extractor, failback
        if (strcmp(extractor, "default")==0 && (email_ret.email == 0 && email_ret.state == PAM_SUCCESS)){
            extract_default(&email_ret, username, param);
        }

        // cleanup
        if (param){
            free(extractor);
        }
        // param must be 0 elsewise extractor is incorrectly freed when not copied
        param = 0;
        extractor = 0;
        use_all = 0;
        if (email_ret.state != PAM_SUCCESS)
            goto error_extract_email;
    }

    return email_ret;
error_extract_email:
    if (email_ret.email)
        free(email_ret.email);
    if (param){
        free(extractor);
    }
    email_ret.email=0;
    return email_ret;
}


int pam_sm_authenticate(pam_handle_t *pamh, int flags,
                        int argc, const char **argv){
    struct pam_email_ret_t ret = extract_email(pamh, argc, argv);
    if (ret.email){
        size_t lenemail = strlen(ret.email);
        // +1 char for \0
        char *emailtemp = 0;
        // handle out of memory errors.Try multiple times until giving up
        for (size_t errcount=0; !emailtemp; errcount++){
            emailtemp = (char*)calloc(strlen(PAM_EMAIL)+lenemail+1, sizeof(char));
#ifdef PAM_EMAIL_ALLOC_ERROR_MAX
            if (errcount>PAM_EMAIL_ALLOC_ERROR_MAX)
                return PAM_IGNORE;
#endif
        }
        strncpy(emailtemp, PAM_EMAIL, strlen(PAM_EMAIL)+1);
        strncat(emailtemp, ret.email, lenemail);
        pam_putenv(pamh, emailtemp);
        free(ret.email);
    }
    if (ret.state!=PAM_SUCCESS && ret.state!=PAM_BUF_ERR)
        return PAM_AUTH_ERR;
    else
        return PAM_IGNORE;
}

int pam_sm_setcred(pam_handle_t *pamh, int flags,
                   int argc, const char **argv){
    return PAM_IGNORE;
}

int pam_sm_open_session(pam_handle_t *pamh, int flags,
                        int argc, const char **argv){
    struct pam_email_ret_t ret = extract_email(pamh, argc, argv);
    if (ret.email){
        size_t lenemail = strlen(ret.email);
        // +1 char for \0
        char *emailtemp = 0;
        // handle out of memory errors.Try multiple times until giving up
        for (size_t errcount=0; !emailtemp; errcount++){
            emailtemp = (char*)calloc(strlen(PAM_EMAIL)+lenemail+1, sizeof(char));
#ifdef PAM_EMAIL_ALLOC_ERROR_MAX
            if (errcount>PAM_EMAIL_ALLOC_ERROR_MAX)
                return PAM_IGNORE;
#endif
        }
        strncpy(emailtemp, PAM_EMAIL, strlen(PAM_EMAIL)+1);
        strncat(emailtemp, ret.email, lenemail);
        pam_putenv(pamh, emailtemp);
        free(ret.email);
    }
    if (ret.state!=PAM_SUCCESS && ret.state!=PAM_BUF_ERR)
        return PAM_SESSION_ERR;
    else
        return PAM_IGNORE;
}
int pam_sm_close_session(pam_handle_t *pamh, int flags,
                         int argc, const char **argv){
    return PAM_IGNORE;
}
