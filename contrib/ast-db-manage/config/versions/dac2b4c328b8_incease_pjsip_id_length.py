"""increase pjsip id length

Revision ID: dac2b4c328b8
Revises: f5b0e7427449
Create Date: 2023-09-23 02:15:24.270526

"""

# revision identifiers, used by Alembic.
revision = 'dac2b4c328b8'
down_revision = 'f5b0e7427449'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.alter_column('ps_aors', 'id', type_=sa.String(255))
    op.alter_column('ps_aors', 'outbound_proxy', type_=sa.String(255))

    op.alter_column('ps_auths', 'id', type_=sa.String(255))
    op.alter_column('ps_auths', 'realm', type_=sa.String(255))

    op.alter_column('ps_contacts', 'outbound_proxy', type_=sa.String(255))
    op.alter_column('ps_contacts', 'endpoint', type_=sa.String(255))

    op.alter_column('ps_domain_aliases', 'id', type_=sa.String(255))
    op.alter_column('ps_domain_aliases', 'domain', type_=sa.String(255))

    op.alter_column('ps_endpoint_id_ips', 'id', type_=sa.String(255))
    op.alter_column('ps_endpoint_id_ips', 'endpoint', type_=sa.String(255))

    op.alter_column('ps_endpoints', 'id', type_=sa.String(255))
    op.alter_column('ps_endpoints', 'aors', type_=sa.String(2048))
    op.alter_column('ps_endpoints', 'auth', type_=sa.String(255))
    op.alter_column('ps_endpoints', 'outbound_auth', type_=sa.String(255))
    op.alter_column('ps_endpoints', 'outbound_proxy', type_=sa.String(255))

    op.alter_column('ps_inbound_publications', 'id', type_=sa.String(255))
    op.alter_column('ps_inbound_publications', 'endpoint', type_=sa.String(255))

    op.alter_column('ps_outbound_publishes', 'id', type_=sa.String(255))
    op.alter_column('ps_outbound_publishes', 'outbound_auth', type_=sa.String(255))

    op.alter_column('ps_registrations', 'id', type_=sa.String(255))
    op.alter_column('ps_registrations', 'outbound_auth', type_=sa.String(255))
    op.alter_column('ps_registrations', 'outbound_proxy', type_=sa.String(255))
    op.alter_column('ps_registrations', 'endpoint', type_=sa.String(255))


def downgrade():
    op.alter_column('ps_aors', 'id', type_=sa.String(40))
    op.alter_column('ps_aors', 'outbound_proxy', type_=sa.String(40))

    op.alter_column('ps_auths', 'id', type_=sa.String(40))
    op.alter_column('ps_auths', 'realm', type_=sa.String(40))

    op.alter_column('ps_contacts', 'outbound_proxy', type_=sa.String(40))
    op.alter_column('ps_contacts', 'endpoint', type_=sa.String(40))

    op.alter_column('ps_domain_aliases', 'id', type_=sa.String(40))
    op.alter_column('ps_domain_aliases', 'domain', type_=sa.String(40))

    op.alter_column('ps_endpoint_id_ips', 'id', type_=sa.String(40))
    op.alter_column('ps_endpoint_id_ips', 'endpoint', type_=sa.String(40))

    op.alter_column('ps_endpoints', 'id', type_=sa.String(40))
    op.alter_column('ps_endpoints', 'aors', type_=sa.String(200))
    op.alter_column('ps_endpoints', 'auth', type_=sa.String(40))
    op.alter_column('ps_endpoints', 'outbound_auth', type_=sa.String(40))
    op.alter_column('ps_endpoints', 'outbound_proxy', type_=sa.String(40))

    op.alter_column('ps_inbound_publications', 'id', type_=sa.String(40))
    op.alter_column('ps_inbound_publications', 'endpoint', type_=sa.String(40))

    op.alter_column('ps_outbound_publishes', 'id', type_=sa.String(40))
    op.alter_column('ps_outbound_publishes', 'outbound_auth', type_=sa.String(40))

    op.alter_column('ps_registrations', 'id', type_=sa.String(40))
    op.alter_column('ps_registrations', 'outbound_auth', type_=sa.String(40))
    op.alter_column('ps_registrations', 'outbound_proxy', type_=sa.String(40))
    op.alter_column('ps_registrations', 'endpoint', type_=sa.String(40))

