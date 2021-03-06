#!/usr/bin/env perl
use strict;
use warnings;
use Test::More;

BEGIN {
    plan skip_all => "xs version requires 5.11.2" unless $] >= 5.011002;
}

my $foo = bless {};
my $bar = bless {};

eval '$foo ~~ $bar';
my $core_error = $@;
$core_error =~ s/\d+/XXX/g;
(my $short_core_error = $core_error) =~ s/ at .* line .*//;

{
    use smartmatch 'core';
    eval '$foo ~~ $bar';
    my $engine_error = $@;
    $engine_error =~ s/\d+/XXX/g;
    (my $short_engine_error = $engine_error) =~ s/ at .* line .*//;
    is($short_engine_error, $short_core_error);
    is($engine_error, $core_error);
}

done_testing;
