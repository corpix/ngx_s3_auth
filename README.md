# S3 proxy module

This nginx module can proxy requests to authenticated S3 backends using V4 authentication API.

## Usage example

Implements proxying of authenticated requests to S3.

> Currently only support GET and HEAD methods.
> Signing request body for other methods is complex and has not yet been implemented.

```nginx
server {
    listen 8080;
    listen [::]:8080;

    server_name ~^(?<bucket>.+)\.localhost$;

    # valid 1 week
    s3_key_scope "20210409/us-east/s3/aws4_request";
    s3_signing_key "/kPgkE1TC891QRKq7bO9PFd4VpE8r3D0ibjj4ZD19Uc="; # ./tools/s3-auth-gen -r us-east -k minioadmin

    s3_access_key "minioadmin";
    s3_endpoint "127.0.0.1:9000";

    location ~ ^/.*$ {
        rewrite ^(.*)$ /$bucket$1 break;
        s3_sign;

        proxy_pass http://127.0.0.1:9000;

        proxy_http_version 1.1;
        proxy_redirect off;
        proxy_buffering on;
        proxy_intercept_errors on;
    }
}
```

List bucket with `curl`:

> Specifying bucket name as subdomain to be `bucket-name`.

```console
$ curl -H'Host: bucket-name.localhost' http://localhost:8080/
...
```

## Credits

This is a refactored fork of the [anomalizer/ngx_aws_auth](https://github.com/anomalizer/ngx_aws_auth) module.
