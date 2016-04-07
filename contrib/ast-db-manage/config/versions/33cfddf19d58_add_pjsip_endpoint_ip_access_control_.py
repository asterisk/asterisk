"""Add PJSIP Endpoint IP Access Control options

Revision ID: 33cfddf19d58
Revises: 1c688d9a003c
Create Date: 2016-04-12 11:50:42.925618

"""

# revision identifiers, used by Alembic.
revision = '33cfddf19d58'
down_revision = '1c688d9a003c'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_endpoints', sa.Column('deny', sa.String(95)))
    op.add_column('ps_endpoints', sa.Column('permit', sa.String(95)))
    op.add_column('ps_endpoints', sa.Column('acl', sa.String(40)))
    op.add_column('ps_endpoints', sa.Column('contact_deny', sa.String(95)))
    op.add_column('ps_endpoints', sa.Column('contact_permit', sa.String(95)))
    op.add_column('ps_endpoints', sa.Column('contact_acl', sa.String(40)))


def downgrade():
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.drop_column('contact_acl')
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.drop_column('contact_permit')
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.drop_column('contact_deny')
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.drop_column('acl')
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.drop_column('permit')
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.drop_column('deny')
