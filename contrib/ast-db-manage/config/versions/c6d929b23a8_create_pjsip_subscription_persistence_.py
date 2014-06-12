"""create pjsip subscription persistence table

Revision ID: c6d929b23a8
Revises: e96a0b8071c
Create Date: 2014-06-06 02:17:38.660447

"""

# revision identifiers, used by Alembic.
revision = 'c6d929b23a8'
down_revision = 'e96a0b8071c'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.create_table(
        'ps_subscription_persistence',
        sa.Column('id', sa.String(40), nullable=False, unique=True),
	sa.Column('packet', sa.String(2048)),
	sa.Column('src_name', sa.String(128)),
	sa.Column('src_port', sa.Integer),
	sa.Column('transport_key', sa.String(64)),
	sa.Column('local_name', sa.String(128)),
	sa.Column('local_port', sa.Integer),
	sa.Column('cseq', sa.Integer),
	sa.Column('tag', sa.String(128)),
	sa.Column('endpoint', sa.String(40)),
	sa.Column('expires', sa.Integer),
    )

    op.create_index('ps_subscription_persistence_id', 'ps_subscription_persistence', ['id'])

def downgrade():
	op.drop_table('ps_subscription_persistence')
