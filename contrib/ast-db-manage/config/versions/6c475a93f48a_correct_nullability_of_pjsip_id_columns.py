"""correct nullability of pjsip id columns

Revision ID: 6c475a93f48a
Revises: d5122576cca8
Create Date: 2024-04-06 09:48:33.116410

"""

# revision identifiers, used by Alembic.
revision = '6c475a93f48a'
down_revision = 'd5122576cca8'

from alembic import op
import sqlalchemy as sa

PJSIP_TABLES = [ 'ps_aors',
                 'ps_auths',
                 'ps_domain_aliases',
                 'ps_endpoint_id_ips',
                 'ps_endpoints',
                 'ps_inbound_publications',
                 'ps_outbound_publishes',
                 'ps_registrations' ]

def upgrade():
    for table_name in PJSIP_TABLES:
        with op.batch_alter_table(table_name) as batch_op:
            batch_op.alter_column('id', nullable=False,
                                  existing_type=sa.String(255), existing_server_default=None,
                                  existing_nullable=True)


def downgrade():
    for table_name in reversed(PJSIP_TABLES):
        with op.batch_alter_table(table_name) as batch_op:
            batch_op.alter_column('id', nullable=True,
                                  existing_type=sa.String(255), existing_server_default=None,
                                  existing_nullable=True)
