package smartmatch::engine::core;
use strict;
use warnings;
use 5.010;
# ABSTRACT: default smartmatch implementation from 5.10 - 5.14

use parent 'DynaLoader';

sub dl_load_flags { 0x01 }

if (!$smartmatch::engine::core::USE_PP && $] >= 5.014) {
    __PACKAGE__->bootstrap(
        # we need to be careful not to touch $VERSION at compile time,
        # otherwise DynaLoader will assume it's set and check against it, which
        # will cause fail when being run in the checkout without dzil having
        # set the actual $VERSION
        exists $smartmatch::engine::core::{VERSION}
            ? ${ $smartmatch::engine::core::{VERSION} } : (),
    );
    init(__PACKAGE__->can('match'));
}

use B;
use Carp qw(croak);
use Hash::Util::FieldHash qw(idhash);
use Scalar::Util qw(blessed looks_like_number reftype);
use overload ();

=head1 SYNOPSIS

  use smartmatch 'core';

=head1 DESCRIPTION

NOTE: This module is still experimental, and the API may change at any point.
You have been warned!

This module implements the existing smart matching algorithm from perl 5.14, as
a module. It has a pure perl implementation of the algorithm (which can be
requested by setting C<$smartmatch::engine::core::USE_PP> to a true value
before C<use>ing this engine), but by default it uses a C implementation which
should be identical to the algorithm in 5.14 - this module uses some new
compiler hooks to turn calls to the engine's C<match> function into a custom
opcode, which is implemented by a copy of the smart match code from perl 5.14.

=cut

sub type {
    my ($thing) = @_;

    if (!defined($thing)) {
        return 'undef';
    }
    elsif (re::is_regexp($thing)) {
        return 'Regex';
    }
    elsif (blessed($thing)) {
        return 'Object';
    }
    elsif (my $reftype = reftype($thing)) {
        if ($reftype eq 'ARRAY') {
            return 'Array';
        }
        elsif ($reftype eq 'HASH') {
            return 'Hash';
        }
        elsif ($reftype eq 'CODE') {
            return 'CodeRef';
        }
        else {
            return 'unknown ref';
        }
    }
    else {
        my $b = B::svref_2object(\$thing);
        my $flags = $b->FLAGS;
        if ($flags & (B::SVf_IOK | B::SVf_NOK)) {
            return 'Num';
        }
        elsif (looks_like_number($thing)) {
            return 'numish';
        }
        else {
            return 'unknown';
        }
    }
}

sub match {
    my ($a, $b, $seen) = @_;

    my $type_a = type($a);
    my $type_b = type($b);

    if ($type_b eq 'undef') {
        return !defined($a);
    }
    elsif ($type_b eq 'Object') {
        my $overload = overload::Method($b, '~~');

        # XXX this is buggy behavior and may be changed
        # see http://www.xray.mpe.mpg.de/mailing-lists/perl5-porters/2011-07/msg00214.html
        if (!$overload && overload::Overloaded($b)) {
            $overload = overload::Method($a, '~~');
            return $a->$overload($b, 0)
                if $overload;
        }

        croak("Smart matching a non-overloaded object breaks encapsulation")
            unless $overload;
        return $b->$overload($a, 1);
    }
    elsif ($type_b eq 'CodeRef') {
        if ($type_a eq 'Hash') {
            return !grep { !$b->($_) } keys %$a;
        }
        elsif ($type_a eq 'Array') {
            return !grep { !$b->($_) } @$a;
        }
        else {
            return $b->($a);
        }
    }
    elsif ($type_b eq 'Hash') {
        if ($type_a eq 'Hash') {
            my @a = sort keys %$a;
            my @b = sort keys %$b;
            return unless @a == @b;
            for my $i (0..$#a) {
                return unless $a[$i] eq $b[$i];
            }
            return 1;
        }
        elsif ($type_a eq 'Array') {
            return grep { exists $b->{$_ // ''} } @$a;
        }
        elsif ($type_a eq 'Regex') {
            return grep /$a/, keys %$b;
        }
        elsif ($type_a eq 'undef') {
            return;
        }
        else {
            return exists $b->{$a};
        }
    }
    elsif ($type_b eq 'Array') {
        if ($type_a eq 'Hash') {
            return grep { exists $a->{$_ // ''} } @$b;
        }
        elsif ($type_a eq 'Array') {
            return unless @$a == @$b;
            if (!$seen) {
                $seen = {};
                idhash %$seen;
            }
            for my $i (0..$#$a) {
                if (defined($b->[$i]) && $seen->{$b->[$i]}++) {
                    return $a->[$i] == $b->[$i];
                }
                return unless match($a->[$i], $b->[$i], $seen);
            }
            return 1;
        }
        elsif ($type_a eq 'Regex') {
            return grep /$a/, @$b;
        }
        elsif ($type_a eq 'undef') {
            return grep !defined, @$b;
        }
        else {
            if (!$seen) {
                $seen = {};
                idhash %$seen;
            }
            return grep {
                if (defined($_) && $seen->{$_}++) {
                    return $a == $_;
                }
                match($a, $_, $seen)
            } @$b;
        }
    }
    elsif ($type_b eq 'Regex') {
        if ($type_a eq 'Hash') {
            return grep /$b/, keys %$a;
        }
        elsif ($type_a eq 'Array') {
            return grep /$b/, @$a;
        }
        else {
            no warnings 'uninitialized';
            return $a =~ $b;
        }
    }
    elsif ($type_a eq 'Object') {
        my $overload = overload::Method($a, '~~');
        return $a->$overload($b, 0) if $overload;
    }

    # XXX perlsyn currently has this undef case after the Num cases, but that's
    # not how it's currently implemented
    if ($type_a eq 'undef') {
        return !defined($b);
    }
    elsif ($type_b eq 'Num') {
        no warnings 'uninitialized', 'numeric'; # ugh
        return $a == $b;
    }
    elsif ($type_a eq 'Num' && $type_b eq 'numish') {
        return $a == $b;
    }
    else {
        return $a eq $b;
    }
}

=head1 BUGS

No known bugs.

Please report any bugs through RT: email
C<bug-smartmatch-engine-core at rt.cpan.org>, or browse to
L<http://rt.cpan.org/NoAuth/ReportBug.html?Queue=smartmatch-engine-core>.

=head1 SEE ALSO

L<smartmatch>

L<perlsyn/"Smart matching in detail">

=head1 SUPPORT

You can find this documentation for this module with the perldoc command.

    perldoc smartmatch::engine::core

You can also look for information at:

=over 4

=item * AnnoCPAN: Annotated CPAN documentation

L<http://annocpan.org/dist/smartmatch-engine-core>

=item * CPAN Ratings

L<http://cpanratings.perl.org/d/smartmatch-engine-core>

=item * RT: CPAN's request tracker

L<http://rt.cpan.org/NoAuth/Bugs.html?Dist=smartmatch-engine-core>

=item * Search CPAN

L<http://search.cpan.org/dist/smartmatch-engine-core>

=back

=begin Pod::Coverage

type
match
init

=end Pod::Coverage

=cut

1;
